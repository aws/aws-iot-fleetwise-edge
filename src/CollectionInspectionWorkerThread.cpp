// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CollectionInspectionWorkerThread.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

CollectionInspectionWorkerThread::CollectionInspectionWorkerThread(
    CollectionInspectionEngine &collectionInspectionEngine,
    std::shared_ptr<SignalBuffer> inputSignalBuffer,
    std::shared_ptr<DataSenderQueue> outputCollectedData,
    uint32_t idleTimeMs,
    RawData::BufferManager *rawDataBufferManager )
    : mCollectionInspectionEngine( collectionInspectionEngine )
    , mInputSignalBuffer( std::move( inputSignalBuffer ) )
    , mOutputCollectedData( std::move( outputCollectedData ) )
    , mRawDataBufferManager( rawDataBufferManager )
{
    if ( idleTimeMs != 0 )
    {
        mIdleTimeMs = idleTimeMs;
    }
}

bool
CollectionInspectionWorkerThread::start()
{
    if ( ( mInputSignalBuffer == nullptr ) || ( mOutputCollectedData == nullptr ) )
    {
        FWE_LOG_ERROR( "Collection Engine cannot be started without correct configurations" );
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
        FWE_LOG_TRACE( "Inspection Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Inspection Thread started" );
        mThread.setThreadName( "fwDICollInsEng" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
CollectionInspectionWorkerThread::stop()
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
CollectionInspectionWorkerThread::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
CollectionInspectionWorkerThread::onChangeInspectionMatrix( std::shared_ptr<const InspectionMatrix> inspectionMatrix )
{
    std::lock_guard<std::mutex> lock( mInspectionMatrixMutex );
    mUpdatedInspectionMatrix = std::move( inspectionMatrix );
    mUpdatedInspectionMatrixAvailable = true;
    FWE_LOG_TRACE( "New inspection matrix handed over" );
    // Wake up the thread.
    mWait.notify();
}

void
CollectionInspectionWorkerThread::onNewDataAvailable()
{
    mWait.notify();
}

void
CollectionInspectionWorkerThread::doWork()
{
    TimePoint lastTimeEvaluated = { 0, 0 };
    Timestamp lastTraceOutput = 0;
    uint32_t statisticInputMessagesProcessed = 0;
    uint32_t statisticDataSentOut = 0;
    uint32_t activations = 0;
    while ( true )
    {
        activations++;
        if ( mUpdatedInspectionMatrixAvailable )
        {
            std::shared_ptr<const InspectionMatrix> newInspectionMatrix;
            {
                std::lock_guard<std::mutex> lock( mInspectionMatrixMutex );
                mUpdatedInspectionMatrixAvailable = false;
                newInspectionMatrix = mUpdatedInspectionMatrix;
            }

            mCollectionInspectionEngine.onChangeInspectionMatrix( std::move( newInspectionMatrix ),
                                                                  mClock->timeSinceEpoch() );
        }
        // Only run the main inspection loop if there is an inspection matrix
        // Otherwise, go to sleep.
        std::shared_ptr<const InspectionMatrix> updatedInspectionMatrix;
        {
            std::lock_guard<std::mutex> lock( mInspectionMatrixMutex );
            updatedInspectionMatrix = mUpdatedInspectionMatrix;
        }
        if ( updatedInspectionMatrix )
        {
            TimePoint currentTime = mClock->timeSinceEpoch();
            uint32_t waitTimeMs = mIdleTimeMs;
            // Consume any new signals and pass them over to the inspection Engine
            auto consumeSignalGroups = [&]( const CollectedDataFrame &dataFrame ) {
                TraceModule::get().incrementVariable( TraceVariable::CE_PROCESSED_DATA_FRAMES );

                if ( !dataFrame.mCollectedSignals.empty() )
                {
                    for ( auto &inputSignal : dataFrame.mCollectedSignals )
                    {
                        TraceModule::get().decrementAtomicVariable(
                            TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                        TraceModule::get().incrementVariable( TraceVariable::CE_PROCESSED_SIGNALS );
                        auto signalValue = inputSignal.getValue();
                        switch ( signalValue.getType() )
                        {
                        case SignalType::UINT8:
                            mCollectionInspectionEngine.addNewSignal<uint8_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.uint8Val );
                            break;
                        case SignalType::INT8:
                            mCollectionInspectionEngine.addNewSignal<int8_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.int8Val );
                            break;
                        case SignalType::UINT16:
                            mCollectionInspectionEngine.addNewSignal<uint16_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.uint16Val );
                            break;
                        case SignalType::INT16:
                            mCollectionInspectionEngine.addNewSignal<int16_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.int16Val );
                            break;
                        case SignalType::UINT32:
                            mCollectionInspectionEngine.addNewSignal<uint32_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.uint32Val );
                            break;
                        case SignalType::INT32:
                            mCollectionInspectionEngine.addNewSignal<int32_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.int32Val );
                            break;
                        case SignalType::UINT64:
                            mCollectionInspectionEngine.addNewSignal<uint64_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.uint64Val );
                            break;
                        case SignalType::INT64:
                            mCollectionInspectionEngine.addNewSignal<int64_t>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.int64Val );
                            break;
                        case SignalType::FLOAT:
                            mCollectionInspectionEngine.addNewSignal<float>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.floatVal );
                            break;
                        case SignalType::DOUBLE:
                            mCollectionInspectionEngine.addNewSignal<double>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.doubleVal );
                            break;
                        case SignalType::BOOLEAN:
                            mCollectionInspectionEngine.addNewSignal<bool>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.boolVal );
                            break;
                        case SignalType::STRING:
                            mCollectionInspectionEngine.addNewSignal<RawData::BufferHandle>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.uint32Val );
                            if ( mRawDataBufferManager != nullptr )
                            {
                                mRawDataBufferManager->decreaseHandleUsageHint(
                                    inputSignal.signalID,
                                    signalValue.value.uint32Val,
                                    RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );
                            }
                            break;
                        case SignalType::UNKNOWN:
                            FWE_LOG_WARN( "UNKNOWN signal [signal id: " + std::to_string( inputSignal.signalID ) +
                                          " ] should not be processed" );
                            break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                        case SignalType::COMPLEX_SIGNAL:
                            mCollectionInspectionEngine.addNewSignal<RawData::BufferHandle>(
                                inputSignal.signalID,
                                inputSignal.fetchRequestID,
                                timePointFromSystemTime( currentTime, inputSignal.receiveTime ),
                                currentTime.monotonicTimeMs,
                                signalValue.value.uint32Val );
                            if ( mRawDataBufferManager != nullptr )
                            {
                                mRawDataBufferManager->decreaseHandleUsageHint(
                                    inputSignal.signalID,
                                    signalValue.value.uint32Val,
                                    RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );
                            }
                            break;
#endif
                        }
                        statisticInputMessagesProcessed++;
                    }
                }

                // Consume any Active DTCs
                // We could check if the DTCs have changed here, but not necessary
                // as we are looking at only the latest known DTCs.
                // We only pop one item from the Buffer for a reason : DTCs represent
                // the health of all ECUs in the network. The Inspection Engine does
                // not need to know that topology and thus counts on the OBD Module
                // to aggregate all DTCs from all ECUs in one single Item.
                if ( dataFrame.mActiveDTCs != nullptr )
                {
                    TraceModule::get().decrementAtomicVariable(
                        TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_DTCS );
                    TraceModule::get().incrementVariable( TraceVariable::CE_PROCESSED_DTCS );
                    mCollectionInspectionEngine.setActiveDTCs( *dataFrame.mActiveDTCs );
                    statisticInputMessagesProcessed++;
                }

                lastTimeEvaluated = mClock->timeSinceEpoch();
                mCollectionInspectionEngine.evaluateConditions( lastTimeEvaluated );

                // Initiate data collection and upload after every condition evaluation
                statisticDataSentOut += collectDataAndUpload( waitTimeMs );
            };
            auto consumed = mInputSignalBuffer->consumeAll( consumeSignalGroups );

            // If nothing was consumed and at least the evaluate interval has elapsed, evaluate the
            // conditions to check heartbeat campaigns:
            if ( ( consumed == 0 ) && ( ( mClock->monotonicTimeSinceEpochMs() - lastTimeEvaluated.monotonicTimeMs ) >=
                                        EVALUATE_INTERVAL_MS ) )
            {
                lastTimeEvaluated = mClock->timeSinceEpoch();
                mCollectionInspectionEngine.evaluateConditions( lastTimeEvaluated );
                statisticDataSentOut += collectDataAndUpload( waitTimeMs );
            }

            // Nothing is in the ring buffer to consume. Go to idle mode for some time.
            uint32_t timeToWait = std::min( waitTimeMs, mIdleTimeMs );
            // Print only every THREAD_IDLE_TIME_MS to avoid console spam
            if ( mClock->monotonicTimeSinceEpochMs() > ( lastTraceOutput + LoggingModule::LOG_AGGREGATION_TIME_MS ) )
            {
                FWE_LOG_TRACE( "Activations: " + std::to_string( activations ) +
                               ". Waiting for some data to come. Idling for: " + std::to_string( timeToWait ) +
                               " ms or until notify. Since last idling processed " +
                               std::to_string( statisticInputMessagesProcessed ) +
                               " incoming data packages and sent out " + std::to_string( statisticDataSentOut ) +
                               " packages out" );
                activations = 0;
                statisticInputMessagesProcessed = 0;
                statisticDataSentOut = 0;
                lastTraceOutput = mClock->monotonicTimeSinceEpochMs();
            }
            mWait.wait( timeToWait );
        }
        else
        {
            // No inspection Matrix available. Wait for it from the CollectionScheme manager
            mWait.wait( Signal::WaitWithPredicate );
        }
        if ( shouldStop() )
        {
            break;
        }
    }
}

uint32_t
CollectionInspectionWorkerThread::collectDataAndUpload( uint32_t &waitTimeMs )
{
    uint32_t collectedDataPackages = 0;

    auto collectedData =
        this->mCollectionInspectionEngine.collectNextDataToSend( this->mClock->timeSinceEpoch(), waitTimeMs );
    while ( ( ( collectedData.triggeredCollectionSchemeData != nullptr )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
              || ( collectedData.triggeredVisionSystemData != nullptr )
#endif
                  ) &&
            ( !this->shouldStop() ) )
    {
        TraceModule::get().incrementVariable( TraceVariable::CE_TRIGGERS );
        if ( collectedData.triggeredCollectionSchemeData != nullptr )
        {
            if ( this->mOutputCollectedData->push( collectedData.triggeredCollectionSchemeData ) )
            {
                collectedDataPackages++;
            }
            else
            {
                FWE_LOG_WARN( "Collected data output buffer is full" );
            }
        }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        if ( collectedData.triggeredVisionSystemData != nullptr )
        {
            if ( this->mOutputCollectedData->push( collectedData.triggeredVisionSystemData ) )
            {
                collectedDataPackages++;
            }
            else
            {
                FWE_LOG_WARN( "Collected data output buffer is full, Vision System Data could not be pushed" );
            }
        }
#endif

        collectedData =
            this->mCollectionInspectionEngine.collectNextDataToSend( this->mClock->timeSinceEpoch(), waitTimeMs );
    }
    return collectedDataPackages;
}

bool
CollectionInspectionWorkerThread::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

CollectionInspectionWorkerThread::~CollectionInspectionWorkerThread()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
