// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManagerWorkerThread.h"
#include "LoggingModule.h"
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include "TraceModule.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

const uint32_t DataSenderManagerWorkerThread::MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG = 6;

DataSenderManagerWorkerThread::DataSenderManagerWorkerThread(
    std::shared_ptr<IConnectivityModule> connectivityModule,
    std::shared_ptr<DataSenderManager> dataSenderManager,
    uint64_t persistencyUploadRetryIntervalMs,
    std::shared_ptr<CollectedDataReadyToPublish> &collectedDataQueue )
    : mCollectedDataQueue( collectedDataQueue )
    , mPersistencyUploadRetryIntervalMs{ persistencyUploadRetryIntervalMs }
    , mDataSenderManager( std::move( dataSenderManager ) )
    , mConnectivityModule( std::move( connectivityModule ) )
{
}

bool
DataSenderManagerWorkerThread::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        FWE_LOG_TRACE( "Data Sender Manager Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Data Sender Manager Thread started" );
        mThread.setThreadName( "fwDSDataSendMng" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
DataSenderManagerWorkerThread::stop()
{
    // It might take several seconds to finish all running S3 async PutObject requests
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
DataSenderManagerWorkerThread::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
DataSenderManagerWorkerThread::doWork( void *data )
{
    DataSenderManagerWorkerThread *sender = static_cast<DataSenderManagerWorkerThread *>( data );

    bool uploadedPersistedDataOnce = false;

    while ( !sender->shouldStop() )
    {
        sender->mTimer.reset();
        uint64_t minTimeToWaitMs = UINT64_MAX;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        // Check if new collection scheme is available to update available senders
        if ( sender->mUpdatedCollectionSchemeListAvailable )
        {
            std::lock_guard<std::mutex> lock( sender->mActiveCollectionSchemesMutex );
            sender->mDataSenderManager->onChangeCollectionSchemeList( sender->mActiveCollectionSchemes );
            sender->mUpdatedCollectionSchemeListAvailable = false;
        }
#endif

        if ( sender->mPersistencyUploadRetryIntervalMs > 0 )
        {
            uint64_t timeToWaitMs =
                sender->mPersistencyUploadRetryIntervalMs -
                std::min( static_cast<uint64_t>( sender->mRetrySendingPersistedDataTimer.getElapsedMs().count() ),
                          sender->mPersistencyUploadRetryIntervalMs );
            minTimeToWaitMs = std::min( minTimeToWaitMs, timeToWaitMs );
        }

        if ( minTimeToWaitMs < UINT64_MAX )
        {
            FWE_LOG_TRACE( "Waiting for: " + std::to_string( minTimeToWaitMs ) + " ms. Persistency " +
                           std::to_string( sender->mPersistencyUploadRetryIntervalMs ) + " configured, " +
                           std::to_string( sender->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) +
                           " timer." );
            sender->mWait.wait( static_cast<uint32_t>( minTimeToWaitMs ) );
        }
        else
        {
            sender->mWait.wait( Signal::WaitWithPredicate );
            auto elapsedTimeMs = sender->mTimer.getElapsedMs().count();
            FWE_LOG_TRACE( "Event arrived. Time elapsed waiting for the event: " + std::to_string( elapsedTimeMs ) +
                           " ms" );
        }

        // Dequeues the collected data queue and sends the data to cloud
        auto consumeData = [&]( const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr ) {
            // Only used for trace logging
            std::string firstSignalValues = "[";
            uint32_t signalPrintCounter = 0;
            std::string firstSignalTimestamp;
            for ( auto &s : triggeredCollectionSchemeDataPtr->signals )
            {
                if ( firstSignalTimestamp.empty() )
                {
                    firstSignalTimestamp = " first signal timestamp: " + std::to_string( s.receiveTime );
                }
                signalPrintCounter++;
                if ( signalPrintCounter > MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG )
                {
                    firstSignalValues += " ...";
                    break;
                }
                auto signalValue = s.getValue();
                firstSignalValues += std::to_string( s.signalID ) + ":";
                switch ( signalValue.getType() )
                {
                case SignalType::UINT8:
                    firstSignalValues += std::to_string( signalValue.value.uint8Val ) + ",";
                    break;
                case SignalType::INT8:
                    firstSignalValues += std::to_string( signalValue.value.int8Val ) + ",";
                    break;
                case SignalType::UINT16:
                    firstSignalValues += std::to_string( signalValue.value.uint16Val ) + ",";
                    break;
                case SignalType::INT16:
                    firstSignalValues += std::to_string( signalValue.value.int16Val ) + ",";
                    break;
                case SignalType::UINT32:
                    firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
                    break;
                case SignalType::INT32:
                    firstSignalValues += std::to_string( signalValue.value.int32Val ) + ",";
                    break;
                case SignalType::UINT64:
                    firstSignalValues += std::to_string( signalValue.value.uint64Val ) + ",";
                    break;
                case SignalType::INT64:
                    firstSignalValues += std::to_string( signalValue.value.int64Val ) + ",";
                    break;
                case SignalType::FLOAT:
                    firstSignalValues += std::to_string( signalValue.value.floatVal ) + ",";
                    break;
                case SignalType::DOUBLE:
                    firstSignalValues += std::to_string( signalValue.value.doubleVal ) + ",";
                    break;
                case SignalType::BOOLEAN:
                    firstSignalValues += std::to_string( static_cast<int>( signalValue.value.boolVal ) ) + ",";
                    break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                case SignalType::RAW_DATA_BUFFER_HANDLE:
                    firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
                    break;
#endif
                }
            }
            firstSignalValues += "]";
            // Avoid invoking Data Collection Sender if there is nothing to send.
            if ( triggeredCollectionSchemeDataPtr->signals.empty() &&
                 triggeredCollectionSchemeDataPtr->canFrames.empty() &&
                 triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.empty()
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                 && triggeredCollectionSchemeDataPtr->uploadedS3Objects.empty()
#endif
            )
            {
                FWE_LOG_INFO(
                    "The trigger for Campaign:  " + triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID +
                    " activated eventID: " + std::to_string( triggeredCollectionSchemeDataPtr->eventID ) +
                    " but no data is available to ingest" );
            }
            else
            {
                std::string message =
                    "FWE data ready to send with eventID " +
                    std::to_string( triggeredCollectionSchemeDataPtr->eventID ) + " from " +
                    triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID +
                    " Signals:" + std::to_string( triggeredCollectionSchemeDataPtr->signals.size() ) + " " +
                    firstSignalValues + firstSignalTimestamp +
                    " trigger timestamp: " + std::to_string( triggeredCollectionSchemeDataPtr->triggerTime ) +
                    " raw CAN frames:" + std::to_string( triggeredCollectionSchemeDataPtr->canFrames.size() ) +
                    " DTCs:" + std::to_string( triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.size() )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                    + " Uploaded S3 Objects: " +
                    std::to_string( triggeredCollectionSchemeDataPtr->uploadedS3Objects.size() )
#endif
                    ;
                FWE_LOG_INFO( message );
                sender->mDataSenderManager->processCollectedData(
                    triggeredCollectionSchemeDataPtr
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                    ,
                    [sender]( TriggeredCollectionSchemeDataPtr uploadedData ) {
                        if ( !sender->mCollectedDataQueue->push( std::move( uploadedData ) ) )
                        {
                            FWE_LOG_WARN( "Collected data output buffer is full" );
                            return;
                        }
                        sender->mWait.notify();
                    }
#endif
                );
            }
        };

        auto consumedElements = sender->mCollectedDataQueue->consumeAll( consumeData );
        TraceModule::get().setVariable( TraceVariable::QUEUE_INSPECTION_TO_SENDER, consumedElements );
        if ( ( !uploadedPersistedDataOnce ) ||
             ( ( sender->mPersistencyUploadRetryIntervalMs > 0 ) &&
               ( static_cast<uint64_t>( sender->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) >=
                 sender->mPersistencyUploadRetryIntervalMs ) ) )
        {
            sender->mRetrySendingPersistedDataTimer.reset();
            if ( sender->mConnectivityModule->isAlive() )
            {
                sender->mDataSenderManager->checkAndSendRetrievedData();
                uploadedPersistedDataOnce = true;
            }
        }
    }
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
void
DataSenderManagerWorkerThread::onChangeCollectionSchemeList(
    const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes )
{
    std::lock_guard<std::mutex> lock( mActiveCollectionSchemesMutex );
    mActiveCollectionSchemes = activeCollectionSchemes;
    mUpdatedCollectionSchemeListAvailable = true;
    FWE_LOG_TRACE( "New list of active collections schemes was handed over" );
    mWait.notify();
}
#endif

bool
DataSenderManagerWorkerThread::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

void
DataSenderManagerWorkerThread::onDataReadyToPublish()
{
    mWait.notify();
}

DataSenderManagerWorkerThread::~DataSenderManagerWorkerThread()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
