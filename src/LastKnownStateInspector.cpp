// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateInspector.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <istream>

namespace Aws
{
namespace IoTFleetWise
{

LastKnownStateInspector::LastKnownStateInspector( std::shared_ptr<DataSenderQueue> commandResponses,
                                                  std::shared_ptr<CacheAndPersist> schemaPersistency )
    : mCommandResponses( std::move( commandResponses ) )
    , mSchemaPersistency( std::move( schemaPersistency ) )
{
    restorePersistedMetadata();
}

template <typename T>
void
LastKnownStateInspector::addSignalBuffer( const LastKnownStateSignalInformation &signalIn )
{
    SignalHistoryBuffer<T> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( signalIn.signalID );
    if ( signalHistoryBufferPtr == nullptr )
    {
        FWE_LOG_WARN( "Unable to retrieve signal history buffer for signal " + std::to_string( signalIn.signalID ) );
        return;
    }
    signalHistoryBufferPtr->mSize = MAX_SIGNAL_HISTORY_BUFFER_SIZE;
}

void
LastKnownStateInspector::onStateTemplatesChanged( const StateTemplateList &stateTemplates )
{
    if ( stateTemplates.empty() )
    {
        FWE_LOG_INFO( "No state template available" );
        clearUnused( {} );
        return;
    }

    // If there is no change in the templates, return early:
    if ( mStateTemplates.size() == stateTemplates.size() )
    {
        bool difference = false;
        for ( const auto &stateTemplate : stateTemplates )
        {
            if ( mStateTemplates.find( stateTemplate->id ) == mStateTemplates.end() )
            {
                difference = true;
                break;
            }
        }
        if ( !difference )
        {
            return;
        }
    }

    clearUnused( stateTemplates );
    auto currentTime = mClock->timeSinceEpoch();
    for ( const auto &stateTemplateInfo : stateTemplates )
    {
        bool activated = false;
        Timestamp deactivateAfterMonotonicTimeMs = 0;
        extractMetadataFields( stateTemplateInfo->id, currentTime, activated, deactivateAfterMonotonicTimeMs );
        mStateTemplates.emplace( stateTemplateInfo->id,
                                 StateTemplate{ // Pointers and references into this memory are maintained so
                                                // hold a shared_ptr to it so it does not get deleted
                                                stateTemplateInfo,
                                                activated,
                                                false, // sendSnapshot
                                                deactivateAfterMonotonicTimeMs,
                                                {},
                                                {},
                                                {} } );

        FWE_LOG_TRACE( "Iterating over signals for template " + stateTemplateInfo->id );
        for ( const auto &signalInfo : stateTemplateInfo->signals )
        {
            FWE_LOG_TRACE( "Processing signal " + std::to_string( signalInfo.signalID ) );
            if ( signalInfo.signalID == INVALID_SIGNAL_ID )
            {
                FWE_LOG_ERROR( "A SignalID with value " + std::to_string( INVALID_SIGNAL_ID ) + " is not allowed" );
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::STATE_TEMPLATE_ERROR );
                return;
            }
            auto signalID = signalInfo.signalID;
            mSignalToBufferTypeMap.insert( { signalID, signalInfo.signalType } );

            switch ( signalInfo.signalType )
            {
            case SignalType::UINT8:
                addSignalBuffer<uint8_t>( signalInfo );
                break;
            case SignalType::INT8:
                addSignalBuffer<int8_t>( signalInfo );
                break;
            case SignalType::UINT16:
                addSignalBuffer<uint16_t>( signalInfo );
                break;
            case SignalType::INT16:
                addSignalBuffer<int16_t>( signalInfo );
                break;
            case SignalType::UINT32:
                addSignalBuffer<uint32_t>( signalInfo );
                break;
            case SignalType::INT32:
                addSignalBuffer<int32_t>( signalInfo );
                break;
            case SignalType::UINT64:
                addSignalBuffer<uint64_t>( signalInfo );
                break;
            case SignalType::INT64:
                addSignalBuffer<int64_t>( signalInfo );
                break;
            case SignalType::FLOAT:
                addSignalBuffer<float>( signalInfo );
                break;
            case SignalType::DOUBLE:
                addSignalBuffer<double>( signalInfo );
                break;
            case SignalType::BOOLEAN:
                addSignalBuffer<bool>( signalInfo );
                break;
            default:
                FWE_LOG_WARN( "Unsupported data type for signal with ID " + std::to_string( signalInfo.signalID ) );
                break;
            }
        }
    }
    static_cast<void>( preAllocateBuffers() );

    FWE_LOG_INFO( "Updated Last Known State Inspection Matrix" );
}

void
LastKnownStateInspector::extractMetadataFields( const SyncID &stateTemplateId,
                                                const TimePoint &currentTime,
                                                bool &activated,
                                                Timestamp &deactivateAfterMonotonicTimeMs )
{
    auto &persistedMetadata = mPersistedMetadata["stateTemplates"][stateTemplateId];
    if ( !persistedMetadata.isObject() )
    {
        return;
    }

    if ( persistedMetadata["activated"].isBool() )
    {
        activated = persistedMetadata["activated"].asBool();
    }
    else
    {
        FWE_LOG_WARN( "Invalid persisted metadata for state template with ID: " + stateTemplateId +
                      ". Field 'activated' not present." );
    }

    if ( persistedMetadata["deactivateAfterSystemTimeMs"].isUInt64() )
    {
        auto deactivateAfterSystemTimeMs = persistedMetadata["deactivateAfterSystemTimeMs"].asUInt64();
        if ( deactivateAfterSystemTimeMs > currentTime.systemTimeMs )
        {
            deactivateAfterMonotonicTimeMs =
                currentTime.monotonicTimeMs + ( deactivateAfterSystemTimeMs - currentTime.systemTimeMs );
        }
    }
    else
    {
        FWE_LOG_WARN( "Invalid persisted metadata for state template with ID: " + stateTemplateId +
                      ". Field 'deactivateAfterMonotonicTimeMs' not present." );
    }
}

void
LastKnownStateInspector::onNewCommandReceived( const LastKnownStateCommandRequest &lastKnownStateCommandRequest )
{
    auto it = mStateTemplates.find( lastKnownStateCommandRequest.stateTemplateID );
    if ( it == mStateTemplates.end() )
    {
        FWE_LOG_WARN( "Received a command for missing state template with ID: " +
                      lastKnownStateCommandRequest.stateTemplateID );
        // coverity[check_return]
        mCommandResponses->push(
            std::make_shared<CommandResponse>( lastKnownStateCommandRequest.commandID,
                                               CommandStatus::EXECUTION_FAILED,
                                               REASON_CODE_STATE_TEMPLATE_OUT_OF_SYNC,
                                               "Received a command for missing state template." ) );
        return;
    }

    auto &stateTemplate = it->second;
    CommandReasonCode reasonCode = REASON_CODE_UNSPECIFIED;
    std::string reasonDescription;
    switch ( lastKnownStateCommandRequest.operation )
    {
    case LastKnownStateOperation::ACTIVATE: {
        if ( stateTemplate.activated )
        {
            FWE_LOG_INFO( "Updating already activated state template with ID: " +
                          lastKnownStateCommandRequest.stateTemplateID );
            reasonCode = REASON_CODE_STATE_TEMPLATE_ALREADY_ACTIVATED;
            reasonDescription = REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_ACTIVATED;
        }
        else
        {
            FWE_LOG_INFO( "Activating state template with ID: " + lastKnownStateCommandRequest.stateTemplateID );
        }

        stateTemplate.activated = true;
        stateTemplate.sendSnapshot = true;
        // We want the periodic update start from the time the command was received, so we need to
        // set the lastTriggerTime to the command received time.
        stateTemplate.timeBasedCondition.lastTriggerTime = lastKnownStateCommandRequest.receivedTime;

        Timestamp deactivateAfterSystemTimeMs = 0;
        if ( lastKnownStateCommandRequest.deactivateAfterSeconds == 0 )
        {
            stateTemplate.deactivateAfterMonotonicTimeMs = 0;
        }
        else
        {
            stateTemplate.deactivateAfterMonotonicTimeMs =
                lastKnownStateCommandRequest.receivedTime.monotonicTimeMs +
                ( static_cast<Timestamp>( lastKnownStateCommandRequest.deactivateAfterSeconds ) * 1000 );
            deactivateAfterSystemTimeMs =
                lastKnownStateCommandRequest.receivedTime.systemTimeMs +
                ( static_cast<Timestamp>( lastKnownStateCommandRequest.deactivateAfterSeconds ) * 1000 );
        }
        PersistedStateTemplateMetadata metadata;
        metadata.stateTemplateId = lastKnownStateCommandRequest.stateTemplateID;
        metadata.activated = stateTemplate.activated;
        metadata.deactivateAfterSystemTimeMs = deactivateAfterSystemTimeMs;
        updatePersistedMetadata( metadata );
        break;
    }
    case LastKnownStateOperation::DEACTIVATE:
        if ( stateTemplate.activated )
        {
            deactivateStateTemplate( stateTemplate );
        }
        else
        {
            FWE_LOG_INFO( "Received request to deactivate state template with ID: " +
                          lastKnownStateCommandRequest.stateTemplateID +
                          " which is already deactivated. Ignoring it." );
            reasonCode = REASON_CODE_STATE_TEMPLATE_ALREADY_DEACTIVATED;
            reasonDescription = REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_DEACTIVATED;
        }
        break;
    case LastKnownStateOperation::FETCH_SNAPSHOT:
        FWE_LOG_INFO( "Scheduling a snapshot for state template with ID: " +
                      lastKnownStateCommandRequest.stateTemplateID );
        stateTemplate.sendSnapshot = true;
        break;
    default:
        FWE_LOG_ERROR(
            "Unsupported operation: " + std::to_string( static_cast<int>( lastKnownStateCommandRequest.operation ) ) +
            " for Last Known State command with ID: " + lastKnownStateCommandRequest.commandID );
        mCommandResponses->push( std::make_shared<CommandResponse>(
            lastKnownStateCommandRequest.commandID, CommandStatus::EXECUTION_FAILED, REASON_CODE_NOT_SUPPORTED, "" ) );
        return;
    }

    // coverity[check_return]
    mCommandResponses->push( std::make_shared<CommandResponse>(
        lastKnownStateCommandRequest.commandID, CommandStatus::SUCCEEDED, reasonCode, reasonDescription ) );
}

bool
LastKnownStateInspector::preAllocateBuffers()
{
    // Allocate size
    size_t usedBytes = 0;

    // Allocate Signal Buffer
    for ( auto &bufferVector : mSignalBuffers )
    {
        auto signalID = bufferVector.first;
        auto signalTypeIterator = mSignalToBufferTypeMap.find( signalID );
        if ( signalTypeIterator != mSignalToBufferTypeMap.end() )
        {
            auto signalType = signalTypeIterator->second;
            switch ( signalType )
            {
            case SignalType::UINT8:
                if ( !allocateBuffer<uint8_t>( signalID, usedBytes, sizeof( struct SignalSample<uint8_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::INT8:
                if ( !allocateBuffer<int8_t>( signalID, usedBytes, sizeof( struct SignalSample<int8_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::UINT16:
                if ( !allocateBuffer<uint16_t>( signalID, usedBytes, sizeof( struct SignalSample<uint16_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::INT16:
                if ( !allocateBuffer<int16_t>( signalID, usedBytes, sizeof( struct SignalSample<int16_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::UINT32:
                if ( !allocateBuffer<uint32_t>( signalID, usedBytes, sizeof( struct SignalSample<uint32_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::INT32:
                if ( !allocateBuffer<int32_t>( signalID, usedBytes, sizeof( struct SignalSample<int32_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::UINT64:
                if ( !allocateBuffer<uint64_t>( signalID, usedBytes, sizeof( struct SignalSample<uint64_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::INT64:
                if ( !allocateBuffer<int64_t>( signalID, usedBytes, sizeof( struct SignalSample<int64_t> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::FLOAT:
                if ( !allocateBuffer<float>( signalID, usedBytes, sizeof( struct SignalSample<float> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::DOUBLE:
                if ( !allocateBuffer<double>( signalID, usedBytes, sizeof( struct SignalSample<double> ) ) )
                {
                    return false;
                }
                break;
            case SignalType::BOOLEAN:
                if ( !allocateBuffer<bool>( signalID, usedBytes, sizeof( struct SignalSample<bool> ) ) )
                {
                    return false;
                }
                break;
            default:
                FWE_LOG_WARN( "Unknown type :" + std::to_string( static_cast<uint32_t>( signalType ) ) );
                break;
            }
        }
        else
        {
            FWE_LOG_WARN( "Fail to allocate buffer for Signal with ID " + std::to_string( signalID ) +
                          " due to missing signal type" );
            return false;
        }
    }
    TraceModule::get().setVariable( TraceVariable::LAST_KNOWN_STATE_SIGNAL_HISTORY_BUFFER_SIZE, usedBytes );
    return true;
}

template <typename T>
bool
LastKnownStateInspector::allocateBuffer( SignalID signalID, size_t &usedBytes, size_t signalSampleSize )
{
    SignalHistoryBuffer<T> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( signalID );

    if ( signalHistoryBufferPtr != nullptr )
    {
        auto &buffer = *signalHistoryBufferPtr;
        uint64_t requiredBytes = buffer.mSize * static_cast<uint64_t>( signalSampleSize );
        if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
        {
            FWE_LOG_ERROR( "The requested " + std::to_string( buffer.mSize ) +
                           " number of signal samples leads to a memory requirement  that's above the maximum "
                           "configured of " +
                           std::to_string( MAX_SAMPLE_MEMORY ) + "Bytes" );
            buffer.mSize = 0;
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::STATE_TEMPLATE_ERROR );
            return false;
        }
        usedBytes += static_cast<size_t>( requiredBytes );

        // reserve the size like new[]
        buffer.mBuffer.resize( buffer.mSize );
    }
    return true;
}

void
LastKnownStateInspector::clearUnused( const StateTemplateList &newStateTemplates )
{
    std::set<SignalID> newSignalIds;
    std::set<SyncID> newStateTemplateIds;
    for ( auto &stateTemplate : newStateTemplates )
    {
        newStateTemplateIds.insert( stateTemplate->id );
        for ( auto &signal : stateTemplate->signals )
        {
            newSignalIds.insert( signal.signalID );
        }
    }

    // Delete buffers for signals that are not present in the updated state templates
    std::vector<SignalID> signalIdsToRemove;
    for ( auto &signalBuffer : mSignalBuffers )
    {
        auto signalId = signalBuffer.first;
        if ( newSignalIds.find( signalId ) == newSignalIds.end() )
        {
            signalIdsToRemove.emplace_back( signalId );
        }
    }
    for ( auto signalId : signalIdsToRemove )
    {
        mSignalBuffers.erase( signalId );
        mSignalToBufferTypeMap.erase( signalId );
    }

    std::vector<SyncID> stateTemplatesToRemove;
    for ( auto &stateTemplate : mStateTemplates )
    {
        auto stateTemplateId = stateTemplate.first;
        if ( newStateTemplateIds.find( stateTemplateId ) == newStateTemplateIds.end() )
        {
            stateTemplatesToRemove.emplace_back( stateTemplateId );
        }
    }
    for ( auto stateTemplateId : stateTemplatesToRemove )
    {
        mStateTemplates.erase( stateTemplateId );
    }
}

void
LastKnownStateInspector::deactivateStateTemplate( StateTemplate &stateTemplate )
{
    FWE_LOG_INFO( "Deactivating state template with ID: " + stateTemplate.info->id );
    stateTemplate.activated = false;
    stateTemplate.deactivateAfterMonotonicTimeMs = 0;
    removePersistedMetadata( { stateTemplate.info->id } );
}

template <typename T>
void
LastKnownStateInspector::collectLatestSignal( std::vector<CollectedSignal> &collectedSignals,
                                              SignalID signalID,
                                              Timestamp lastTriggerTime )
{
    SignalHistoryBuffer<T> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( signalID );
    if ( signalHistoryBufferPtr == nullptr )
    {
        // Invalid access to the map Buffer datatype
        FWE_LOG_WARN( "Unable to locate the signal history buffer for signal " + std::to_string( signalID ) );
        return;
    }

    if ( signalHistoryBufferPtr->mCounter == 0 )
    {
        FWE_LOG_WARN( "Can't collect signal as history buffer for signal " + std::to_string( signalID ) + " is empty" );
        return;
    }

    collectedSignals.emplace_back(
        CollectedSignal( signalID,
                         signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].mTimestamp,
                         signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].mValue,
                         mSignalToBufferTypeMap[signalID] ) );

    if ( signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].mTimestamp <= lastTriggerTime )
    {
        // Signal value wasn't updated since last trigger
        TraceModule::get().incrementVariable( TraceVariable::LAST_KNOWN_STATE_NO_SIGNAL_CHANGE_ON_PERIODIC_UPDATE );
    }
}

void
LastKnownStateInspector::collectData( std::vector<CollectedSignal> &collectedSignals,
                                      SignalID signalID,
                                      Timestamp lastTriggerTime )
{
    if ( mSignalToBufferTypeMap.find( signalID ) != mSignalToBufferTypeMap.end() )
    {
        auto signalType = mSignalToBufferTypeMap[signalID];
        switch ( signalType )
        {
        case SignalType::UINT8:
            collectLatestSignal<uint8_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::INT8:
            collectLatestSignal<int8_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::UINT16:
            collectLatestSignal<uint16_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::INT16:
            collectLatestSignal<int16_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::UINT32:
            collectLatestSignal<uint32_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::INT32:
            collectLatestSignal<int32_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::UINT64:
            collectLatestSignal<uint64_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::INT64:
            collectLatestSignal<int64_t>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::FLOAT:
            collectLatestSignal<float>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::DOUBLE:
            collectLatestSignal<double>( collectedSignals, signalID, lastTriggerTime );
            break;
        case SignalType::BOOLEAN:
            collectLatestSignal<bool>( collectedSignals, signalID, lastTriggerTime );
            break;
        default:
            FWE_LOG_WARN( "Unknown type :" + std::to_string( static_cast<uint32_t>( signalType ) ) );
            break;
        }
    }
}

std::shared_ptr<const LastKnownStateCollectedData>
LastKnownStateInspector::collectNextDataToSend( const TimePoint &currentTime )
{
    auto collectedData = std::make_shared<LastKnownStateCollectedData>();
    collectedData->triggerTime = currentTime.systemTimeMs;

    for ( auto &idAndStateTemplate : mStateTemplates )
    {
        auto &stateTemplate = idAndStateTemplate.second;

        if ( ( stateTemplate.deactivateAfterMonotonicTimeMs != 0 ) &&
             ( currentTime.monotonicTimeMs > stateTemplate.deactivateAfterMonotonicTimeMs ) )
        {
            deactivateStateTemplate( stateTemplate );
        }

        std::vector<CollectedSignal> signalsToSend;

        if ( stateTemplate.sendSnapshot )
        {
            stateTemplate.sendSnapshot = false;
            for ( const auto &signalInfo : stateTemplate.info->signals )
            {
                FWE_LOG_TRACE( "Collecting signal with ID " + std::to_string( signalInfo.signalID ) + " for snapshot" );
                collectData( signalsToSend, signalInfo.signalID, 0 );
            }

            // After the snapshot, we have to set all the time based conditions as triggered, otherwise
            // this could cause the same signals to be sent again the next time we collect data.
            stateTemplate.timeBasedCondition.signalIDsToSend.clear();
            stateTemplate.timeBasedCondition.lastTriggerTime = currentTime;
        }
        else if ( stateTemplate.activated )
        {
            signalsToSend = std::move( stateTemplate.changedSignals );

            // Here we are using monotonic clock to check whether time window has satisfied
            if ( currentTime.monotonicTimeMs - stateTemplate.timeBasedCondition.lastTriggerTime.monotonicTimeMs >=
                 stateTemplate.info->periodMs )
            {
                for ( const auto signalID : stateTemplate.timeBasedCondition.signalIDsToSend )
                {
                    FWE_LOG_TRACE( "Collecting signal with ID " + std::to_string( signalID ) +
                                   " for periodical update" );
                    collectData(
                        signalsToSend, signalID, stateTemplate.timeBasedCondition.lastTriggerTime.systemTimeMs );
                    TraceModule::get().incrementVariable( TraceVariable::LAST_KNOWN_STATE_PERIODIC_UPDATES );
                }
                // reset signalID set to be ready for next time window
                stateTemplate.timeBasedCondition.signalIDsToSend.clear();
                stateTemplate.timeBasedCondition.lastTriggerTime = currentTime;
            }
        }

        stateTemplate.changedSignals = std::vector<CollectedSignal>();

        if ( signalsToSend.empty() )
        {
            continue;
        }

        collectedData->stateTemplateCollectedSignals.emplace_back(
            StateTemplateCollectedSignals{ stateTemplate.info->id, std::move( signalsToSend ) } );
    }

    if ( collectedData->stateTemplateCollectedSignals.empty() )
    {
        return nullptr;
    }

    return collectedData;
}

void
LastKnownStateInspector::restorePersistedMetadata()
{
    if ( mSchemaPersistency == nullptr )
    {
        return;
    }

    mPersistedMetadata.clear();
    mPersistedMetadata["stateTemplates"] = Json::Value( Json::objectValue );

    auto fileSize = mSchemaPersistency->getSize( DataType::STATE_TEMPLATE_LIST_METADATA );
    if ( fileSize <= 0 )
    {
        FWE_LOG_INFO(
            "No state template metadata found in persistent storage. All state templates will start as deactivated." );
        return;
    }

    std::vector<uint8_t> fileContent( fileSize );
    if ( mSchemaPersistency->read( fileContent.data(), fileSize, DataType::STATE_TEMPLATE_LIST_METADATA ) !=
         ErrorCode::SUCCESS )
    {
        return;
    }

    Json::CharReaderBuilder builder;
    std::stringstream contentStream;
    contentStream.rdbuf()->pubsetbuf( reinterpret_cast<char *>( fileContent.data() ),
                                      static_cast<std::streamsize>( fileContent.size() ) );
    std::string errors;
    if ( !Json::parseFromStream( builder, contentStream, &mPersistedMetadata, &errors ) )
    {
        FWE_LOG_ERROR( "Failed to parse persisted state template metadata: " + errors );
        return;
    }

    for ( const auto &stateTemplateId : mPersistedMetadata["stateTemplates"].getMemberNames() )
    {
        auto &stateTemplate = mPersistedMetadata["stateTemplates"][stateTemplateId];
        FWE_LOG_INFO( "Restored metadata for state template with ID: " + stateTemplateId + " activated: " +
                      std::to_string( stateTemplate["activated"].asBool() ) + " deactivateAfterSystemTimeMs: " +
                      std::to_string( stateTemplate["deactivateAfterSystemTimeMs"].asInt64() ) );
    }

    FWE_LOG_INFO( "Successfully restored persisted state template metadata" );
}

void
LastKnownStateInspector::updatePersistedMetadata( const PersistedStateTemplateMetadata &metadata )
{
    if ( mSchemaPersistency == nullptr )
    {
        return;
    }

    FWE_LOG_TRACE( "Persisting metadata for state template with ID: " + metadata.stateTemplateId );
    auto &stateTemplateMetadataJson = mPersistedMetadata["stateTemplates"][metadata.stateTemplateId];
    stateTemplateMetadataJson["activated"] = metadata.activated;
    stateTemplateMetadataJson["deactivateAfterSystemTimeMs"] = metadata.deactivateAfterSystemTimeMs;

    Json::StreamWriterBuilder builder;
    std::string output = Json::writeString( builder, mPersistedMetadata );
    mSchemaPersistency->write(
        reinterpret_cast<const uint8_t *>( output.data() ), output.size(), DataType::STATE_TEMPLATE_LIST_METADATA );
}

void
LastKnownStateInspector::removePersistedMetadata( const std::vector<SyncID> &stateTemplateIds )
{
    if ( mSchemaPersistency == nullptr )
    {
        return;
    }

    for ( const auto &stateTemplateId : stateTemplateIds )
    {
        if ( mPersistedMetadata["stateTemplates"].isMember( stateTemplateId ) )
        {
            FWE_LOG_TRACE( "Removing metadata for state template with ID: " + stateTemplateId );
            mPersistedMetadata["stateTemplates"].removeMember( stateTemplateId );
        }
    }

    Json::StreamWriterBuilder builder;
    std::string output = Json::writeString( builder, mPersistedMetadata );
    mSchemaPersistency->write(
        reinterpret_cast<const uint8_t *>( output.data() ), output.size(), DataType::STATE_TEMPLATE_LIST_METADATA );
}

} // namespace IoTFleetWise
} // namespace Aws
