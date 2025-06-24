// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateWorkerThread.h"
#include "aws/iotfleetwise/LastKnownStateTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

LastKnownStateWorkerThread::LastKnownStateWorkerThread(
    std::shared_ptr<SignalBuffer> inputSignalBuffer,
    std::shared_ptr<DataSenderQueue> collectedLastKnownStateData,
    std::unique_ptr<LastKnownStateInspector> lastKnownStateInspector,
    uint32_t idleTimeMs )
    : mInputSignalBuffer( std::move( inputSignalBuffer ) )
    , mCollectedLastKnownStateData( std::move( collectedLastKnownStateData ) )
    , mLastKnownStateInspector( std::move( lastKnownStateInspector ) )
{
    if ( idleTimeMs != 0 )
    {
        mIdleTimeMs = idleTimeMs;
    }
}

bool
LastKnownStateWorkerThread::start()
{
    if ( mInputSignalBuffer == nullptr )
    {
        FWE_LOG_ERROR( "Failed to initialize Last Known State worker thread, no signal buffer provided" );
        return false;
    }

    if ( mCollectedLastKnownStateData == nullptr )
    {
        FWE_LOG_ERROR( "Failed to initialize Last Known State worker thread, no output queue for upload provided" );
        return false;
    }

    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "Last Known State Inspection Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Last Known State Inspection Thread started" );
        mThread.setThreadName( "fwDILKSEng" );
    }

    return mThread.isActive() && mThread.isValid();
}

void
LastKnownStateWorkerThread::doWork()
{
    while ( true )
    {
        {
            std::lock_guard<std::mutex> lock( mStateTemplatesMutex );
            if ( mStateTemplatesAvailable )
            {
                mStateTemplatesAvailable = false;
                mStateTemplates = mStateTemplatesInput;
                mLastKnownStateInspector->onStateTemplatesChanged( *mStateTemplates );
            }
        }

        {
            std::lock_guard<std::mutex> lock( mLastKnownStateCommandsMutex );
            for ( auto &command : mLastKnownStateCommandsInput )
            {
                mLastKnownStateInspector->onNewCommandReceived( command );
            }
            mLastKnownStateCommandsInput.clear();
        }

        // Data should only be processed if state templates are available
        if ( ( mStateTemplates != nullptr ) && ( !mStateTemplates->empty() ) )
        {
            TimePoint currentTime = mClock->timeSinceEpoch();
            auto consumeSignalGroups = [&]( const CollectedDataFrame &dataFrame ) {
                static_cast<void>( dataFrame );

                for ( auto &signal : dataFrame.mCollectedSignals )
                {
                    auto signalValue = signal.getValue();
                    switch ( signalValue.getType() )
                    {
                    case SignalType::UINT8:
                        mLastKnownStateInspector->inspectNewSignal<uint8_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint8Val );
                        break;
                    case SignalType::INT8:
                        mLastKnownStateInspector->inspectNewSignal<int8_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.int8Val );
                        break;
                    case SignalType::UINT16:
                        mLastKnownStateInspector->inspectNewSignal<uint16_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint16Val );
                        break;
                    case SignalType::INT16:
                        mLastKnownStateInspector->inspectNewSignal<int16_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.int16Val );
                        break;
                    case SignalType::UINT32:
                        mLastKnownStateInspector->inspectNewSignal<uint32_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint32Val );
                        break;
                    case SignalType::INT32:
                        mLastKnownStateInspector->inspectNewSignal<int32_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.int32Val );
                        break;
                    case SignalType::UINT64:
                        mLastKnownStateInspector->inspectNewSignal<uint64_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint64Val );
                        break;
                    case SignalType::INT64:
                        mLastKnownStateInspector->inspectNewSignal<int64_t>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.int64Val );
                        break;
                    case SignalType::FLOAT:
                        mLastKnownStateInspector->inspectNewSignal<float>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.floatVal );
                        break;
                    case SignalType::DOUBLE:
                        mLastKnownStateInspector->inspectNewSignal<double>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.doubleVal );
                        break;
                    case SignalType::BOOLEAN:
                        mLastKnownStateInspector->inspectNewSignal<bool>(
                            signal.signalID,
                            timePointFromSystemTime( currentTime, signal.receiveTime ),
                            signalValue.value.boolVal );
                        break;
                    case SignalType::STRING:
                        FWE_LOG_WARN( "String data is not available for last known state collection" );
                        break;
                    case SignalType::UNKNOWN:
                        FWE_LOG_WARN( " Unknown signals [signal ID: " + std::to_string( signal.signalID ) +
                                      " ] not supported for last known state collection" );
                        break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                    case SignalType::COMPLEX_SIGNAL:
                        FWE_LOG_WARN( "Vision system data is not available for last known state collection" );
                        break;
#endif
                    }
                }
            };
            mInputSignalBuffer->consumeAll( consumeSignalGroups );

            // Collect and upload data
            auto collectedData = mLastKnownStateInspector->collectNextDataToSend( mClock->timeSinceEpoch() );
            if ( collectedData != nullptr )
            {
                TraceModule::get().incrementVariable( TraceVariable::LAST_KNOWN_STATE_COLLECTION_TRIGGERS );
                static_cast<void>( mCollectedLastKnownStateData->push( collectedData ) );
            }
            mWait.wait( mIdleTimeMs );
        }
        else
        {
            // Consume all data received so far to prevent the queue from becoming full
            mInputSignalBuffer->consumeAll( [&]( const CollectedDataFrame &dataFrame ) {
                static_cast<void>( dataFrame );
            } );
            // Wait for the state templates to arrive
            mWait.wait( Signal::WaitWithPredicate );
        }

        if ( shouldStop() )
        {
            break;
        }
    }
}

void
LastKnownStateWorkerThread::onNewDataAvailable()
{
    mWait.notify();
}

void
LastKnownStateWorkerThread::onStateTemplatesChanged( std::shared_ptr<StateTemplateList> stateTemplates )
{
    std::lock_guard<std::mutex> lock( mStateTemplatesMutex );
    mStateTemplatesInput = std::move( stateTemplates );
    mStateTemplatesAvailable = true;
    FWE_LOG_TRACE( "State templates were updated" );
    // Wake up the thread.
    mWait.notify();
}

void
LastKnownStateWorkerThread::onNewCommandReceived( const LastKnownStateCommandRequest &commandRequest )
{
    std::lock_guard<std::mutex> lock( mLastKnownStateCommandsMutex );
    mLastKnownStateCommandsInput.push_back( commandRequest );
    mWait.notify();
}

bool
LastKnownStateWorkerThread::stop()
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
LastKnownStateWorkerThread::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
LastKnownStateWorkerThread::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

LastKnownStateWorkerThread::~LastKnownStateWorkerThread()
{
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
