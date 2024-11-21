// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LastKnownStateWorkerThread.h"
#include "LastKnownStateTypes.h"
#include "LoggingModule.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "TraceModule.h"
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
    if ( !mThread.create( doWork, this ) )
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
LastKnownStateWorkerThread::doWork( void *data )
{
    LastKnownStateWorkerThread *consumer = static_cast<LastKnownStateWorkerThread *>( data );

    while ( true )
    {
        {
            std::lock_guard<std::mutex> lock( consumer->mStateTemplatesMutex );
            if ( consumer->mStateTemplatesAvailable )
            {
                consumer->mStateTemplatesAvailable = false;
                consumer->mStateTemplates = consumer->mStateTemplatesInput;
                consumer->mLastKnownStateInspector->onStateTemplatesChanged( consumer->mStateTemplates );
            }
        }

        {
            std::lock_guard<std::mutex> lock( consumer->mLastKnownStateCommandsMutex );
            for ( auto &command : consumer->mLastKnownStateCommandsInput )
            {
                consumer->mLastKnownStateInspector->onNewCommandReceived( command );
            }
            consumer->mLastKnownStateCommandsInput.clear();
        }

        // Data should only be processed if state templates are available
        if ( ( consumer->mStateTemplates != nullptr ) && ( !consumer->mStateTemplates->empty() ) )
        {
            TimePoint currentTime = consumer->mClock->timeSinceEpoch();
            auto consumeSignalGroups = [&]( const CollectedDataFrame &dataFrame ) {
                static_cast<void>( dataFrame );

                for ( auto &signal : dataFrame.mCollectedSignals )
                {
                    auto signalValue = signal.getValue();
                    switch ( signalValue.getType() )
                    {
                    case SignalType::UINT8:
                        consumer->mLastKnownStateInspector->inspectNewSignal<uint8_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint8Val );
                        break;
                    case SignalType::INT8:
                        consumer->mLastKnownStateInspector->inspectNewSignal<int8_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.int8Val );
                        break;
                    case SignalType::UINT16:
                        consumer->mLastKnownStateInspector->inspectNewSignal<uint16_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint16Val );
                        break;
                    case SignalType::INT16:
                        consumer->mLastKnownStateInspector->inspectNewSignal<int16_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.int16Val );
                        break;
                    case SignalType::UINT32:
                        consumer->mLastKnownStateInspector->inspectNewSignal<uint32_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint32Val );
                        break;
                    case SignalType::INT32:
                        consumer->mLastKnownStateInspector->inspectNewSignal<int32_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.int32Val );
                        break;
                    case SignalType::UINT64:
                        consumer->mLastKnownStateInspector->inspectNewSignal<uint64_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.uint64Val );
                        break;
                    case SignalType::INT64:
                        consumer->mLastKnownStateInspector->inspectNewSignal<int64_t>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.int64Val );
                        break;
                    case SignalType::FLOAT:
                        consumer->mLastKnownStateInspector->inspectNewSignal<float>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.floatVal );
                        break;
                    case SignalType::DOUBLE:
                        consumer->mLastKnownStateInspector->inspectNewSignal<double>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
                            signalValue.value.doubleVal );
                        break;
                    case SignalType::BOOLEAN:
                        consumer->mLastKnownStateInspector->inspectNewSignal<bool>(
                            signal.signalID,
                            calculateMonotonicTime( currentTime, signal.receiveTime ),
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
            consumer->mInputSignalBuffer->consumeAll( consumeSignalGroups );

            // Collect and upload data
            auto collectedData =
                consumer->mLastKnownStateInspector->collectNextDataToSend( consumer->mClock->timeSinceEpoch() );
            if ( collectedData != nullptr )
            {
                TraceModule::get().incrementVariable( TraceVariable::LAST_KNOWN_STATE_COLLECTION_TRIGGERS );
                static_cast<void>( consumer->mCollectedLastKnownStateData->push( collectedData ) );
            }
            consumer->mWait.wait( consumer->mIdleTimeMs );
        }
        else
        {
            // Consume all data received so far to prevent the queue from becoming full
            consumer->mInputSignalBuffer->consumeAll( [&]( const CollectedDataFrame &dataFrame ) {
                static_cast<void>( dataFrame );
            } );
            // Wait for the state templates to arrive
            consumer->mWait.wait( Signal::WaitWithPredicate );
        }

        if ( consumer->shouldStop() )
        {
            break;
        }
    }
}

TimePoint
LastKnownStateWorkerThread::calculateMonotonicTime( const TimePoint &currTime, Timestamp systemTimeMs )
{
    TimePoint convertedTime = timePointFromSystemTime( currTime, systemTimeMs );
    if ( ( convertedTime.systemTimeMs == 0 ) && ( convertedTime.monotonicTimeMs == 0 ) )
    {
        FWE_LOG_ERROR( "The system time " + std::to_string( systemTimeMs ) +
                       " corresponds to a time in the past before the monotonic" +
                       " clock started ticking. Current system time: " + std::to_string( currTime.systemTimeMs ) +
                       ". Current monotonic time: " + std::to_string( currTime.monotonicTimeMs ) );
        return TimePoint{ systemTimeMs, 0 };
    }
    return convertedTime;
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
    mStateTemplatesInput = stateTemplates;
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
