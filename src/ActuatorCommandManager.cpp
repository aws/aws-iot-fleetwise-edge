// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/ActuatorCommandManager.h"
#include "aws/iotfleetwise/Assert.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <string>
#include <unordered_map>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

ActuatorCommandManager::ActuatorCommandManager( std::shared_ptr<DataSenderQueue> commandResponses,
                                                size_t maxConcurrentCommandRequests,
                                                RawData::BufferManager *rawDataBufferManager )
    : mMaxConcurrentCommandRequests( maxConcurrentCommandRequests )
    , mCommandResponses( std::move( commandResponses ) )
    , mRawDataBufferManager( rawDataBufferManager )
{
}

void
ActuatorCommandManager::onReceivingCommandRequest( const ActuatorCommandRequest &commandRequest )
{
    std::lock_guard<std::mutex> lock( mCommandRequestMutex );
    if ( mCommandRequestQueue.size() >= mMaxConcurrentCommandRequests )
    {
        FWE_LOG_ERROR( "Command Requests processing queue is full, could not ingest command request" );
        return;
    }
    mCommandRequestQueue.push( commandRequest );
    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::QUEUE_PENDING_COMMAND_REQUESTS );
    FWE_LOG_TRACE( "New command request was handed over" );
    mWait.notify();
}

bool
ActuatorCommandManager::registerDispatcher( const std::string &interfaceId,
                                            std::shared_ptr<ICommandDispatcher> dispatcher )
{
    auto registered = mInterfaceIDToCommandDispatcherMap.emplace( interfaceId, std::move( dispatcher ) ).second;
    if ( !registered )
    {
        FWE_LOG_ERROR( "Dispatcher for interface ID " + interfaceId + " already registered" );
    }
    return registered;
}

std::unordered_map<InterfaceID, std::vector<std::string>>
ActuatorCommandManager::getActuatorNames()
{
    std::unordered_map<InterfaceID, std::vector<std::string>> actuatorNames;
    for ( const auto &interface : mInterfaceIDToCommandDispatcherMap )
    {
        actuatorNames.emplace( interface.first, interface.second->getActuatorNames() );
    }
    return actuatorNames;
}

bool
ActuatorCommandManager::start()
{
    if ( mCommandResponses == nullptr )
    {
        FWE_LOG_ERROR( "No queue provided for the Command Responses" );
        return false;
    }

    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "Command Manager Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Command Manager Thread started" );
        mThread.setThreadName( "fwRCCommandMng" );
    }

    return mThread.isActive() && mThread.isValid();
}

void
ActuatorCommandManager::doWork()
{
    // Initialize dispatchers on this thread to avoid delaying bootstrap. Commands may be received
    // directly after connection to the cloud, which will be queued until this initialization loop
    // has completed.
    for ( auto interfaceDispatcherIt = mInterfaceIDToCommandDispatcherMap.begin();
          ( interfaceDispatcherIt != mInterfaceIDToCommandDispatcherMap.end() ) && ( !shouldStop() );
          interfaceDispatcherIt++ )
    {
        FWE_GRACEFUL_FATAL_ASSERT( interfaceDispatcherIt->second->init(), "Fatal error initializing dispatcher", );
    }

    while ( !shouldStop() )
    {
        mWait.wait( Signal::WaitWithPredicate );

        while ( !mCommandRequestQueue.empty() )
        {
            auto &commandRequest = mCommandRequestQueue.front();
            processCommandRequest( commandRequest );
            mCommandRequestQueue.pop();
            TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_PENDING_COMMAND_REQUESTS );
        }
    }
}

void
ActuatorCommandManager::processCommandRequest( const ActuatorCommandRequest &commandRequest )
{
    FWE_LOG_TRACE( "Processing Command Request with ID: " + commandRequest.commandID );

    if ( commandRequest.decoderID != mCurrentDecoderManifestID )
    {
        FWE_LOG_ERROR( "Decoder manifest sync id does not match with the decoder manifest used by the agent, cannot "
                       "process Command with ID " +
                       commandRequest.commandID );
        queueCommandResponse( commandRequest,
                              CommandStatus::EXECUTION_FAILED,
                              REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC,
                              commandRequest.decoderID + " vs " + mCurrentDecoderManifestID );
        return;
    }

    if ( mCustomSignalDecoderFormatMap == nullptr )
    {
        FWE_LOG_ERROR( "No Custom Signal Decoder Format map was provided, cannot process Command with ID " +
                       commandRequest.commandID );
        queueCommandResponse( commandRequest,
                              CommandStatus::EXECUTION_FAILED,
                              REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC,
                              "No decoder manifest" );
        return;
    }

    // Retrieve Custom Decoder ID from the decoder manifest
    auto customSignalDecoderFormatIt = mCustomSignalDecoderFormatMap->find( commandRequest.signalID );
    if ( customSignalDecoderFormatIt == mCustomSignalDecoderFormatMap->end() )
    {
        FWE_LOG_ERROR( "Command Signal Decoder Format not found for signal ID " +
                       std::to_string( commandRequest.signalID ) + ". Command with ID " + commandRequest.commandID +
                       " can not be processed." );
        queueCommandResponse(
            commandRequest, CommandStatus::EXECUTION_FAILED, REASON_CODE_NO_DECODING_RULES_FOUND, "" );
        return;
    }

    const auto &interfaceId = customSignalDecoderFormatIt->second.mInterfaceId;
    auto commandDispatcherIt = mInterfaceIDToCommandDispatcherMap.find( interfaceId );
    if ( commandDispatcherIt == mInterfaceIDToCommandDispatcherMap.end() )
    {
        FWE_LOG_ERROR( "No command dispatcher found for signal ID " + std::to_string( commandRequest.signalID ) +
                       ", interface ID " + interfaceId + ". Command with ID " + commandRequest.commandID +
                       " can not be processed." );
        queueCommandResponse(
            commandRequest, CommandStatus::EXECUTION_FAILED, REASON_CODE_NO_COMMAND_DISPATCHER_FOUND, "" );
        return;
    }
    const auto &actuatorName = customSignalDecoderFormatIt->second.mDecoder;

    // Timeout is already checked during command reception from the cloud, but check again here in case
    // command dispatching is being run synchronously and a large delay has occurred since the command
    // was received.
    if ( ( commandRequest.executionTimeoutMs > 0 ) &&
         ( ( commandRequest.issuedTimestampMs + commandRequest.executionTimeoutMs ) <=
           mClock->systemTimeSinceEpochMs() ) )
    {
        FWE_LOG_ERROR( "Command Request with ID " + commandRequest.commandID + " timed out" );
        queueCommandResponse(
            commandRequest, CommandStatus::EXECUTION_TIMEOUT, REASON_CODE_TIMED_OUT_BEFORE_DISPATCH, "" );
        return;
    }

    // Send command request to the command dispatcher
    commandDispatcherIt->second->setActuatorValue(
        actuatorName,
        commandRequest.signalValueWrapper,
        commandRequest.commandID,
        commandRequest.issuedTimestampMs,
        commandRequest.executionTimeoutMs,
        [this, commandRequest](
            CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription ) {
            queueCommandResponse( commandRequest, status, reasonCode, reasonDescription );
        } );
}

void
ActuatorCommandManager::queueCommandResponse( const ActuatorCommandRequest &commandRequest,
                                              CommandStatus commandStatus,
                                              CommandReasonCode reasonCode,
                                              const CommandReasonDescription &reasonDescription )
{
    if ( commandRequest.signalValueWrapper.type == SignalType::STRING )
    {
        mRawDataBufferManager->decreaseHandleUsageHint( commandRequest.signalValueWrapper.value.rawDataVal.signalId,
                                                        commandRequest.signalValueWrapper.value.rawDataVal.handle,
                                                        RawData::BufferHandleUsageStage::UPLOADING );
    }

    // Emit metrics for command execution
    if ( reasonCode == REASON_CODE_PRECONDITION_FAILED )
    {
        TraceModule::get().incrementVariable( TraceVariable::COMMAND_PRECONDITION_CHECK_FAILURE );
    }
    else if ( reasonCode == REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC )
    {
        TraceModule::get().incrementVariable( TraceVariable::COMMAND_DECODER_MANIFEST_FAILURE );
    }
    else if ( commandStatus == CommandStatus::EXECUTION_TIMEOUT )
    {
        TraceModule::get().incrementVariable( TraceVariable::COMMAND_EXECUTION_TIMEOUT );
    }
    else if ( commandStatus == CommandStatus::EXECUTION_FAILED )
    {
        TraceModule::get().incrementVariable( TraceVariable::COMMAND_EXECUTION_FAILURE );
    }
    else
    {
        // Do nothing
    }

    // coverity[check_return]
    mCommandResponses->push(
        std::make_shared<CommandResponse>( commandRequest.commandID, commandStatus, reasonCode, reasonDescription ) );
}

void
ActuatorCommandManager::onChangeOfCustomSignalDecoderFormatMap(
    const SyncID &currentDecoderManifestID,
    const SignalIDToCustomSignalDecoderFormatMapPtr &customSignalDecoderFormatMap )
{
    std::lock_guard<std::mutex> lock( mCustomSignalDecoderFormatMapUpdateMutex );
    mCustomSignalDecoderFormatMap = customSignalDecoderFormatMap;
    mCurrentDecoderManifestID = currentDecoderManifestID;
    FWE_LOG_TRACE( "Custom Signal Decoder Format Map was handed over to the Command Manager" );
}

bool
ActuatorCommandManager::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
ActuatorCommandManager::stop()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return true;
    }
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Request stop" );
    mWait.notify();
    mThread.release();
    FWE_LOG_TRACE( "Stop finished" );
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
ActuatorCommandManager::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

ActuatorCommandManager::~ActuatorCommandManager()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
