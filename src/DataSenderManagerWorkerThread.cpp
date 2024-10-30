// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManagerWorkerThread.h"
#include "DataSenderTypes.h"
#include "LoggingModule.h"
#include "QueueTypes.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

const uint32_t DataSenderManagerWorkerThread::MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG = 6;

DataSenderManagerWorkerThread::DataSenderManagerWorkerThread(
    std::shared_ptr<IConnectivityModule> connectivityModule,
    std::shared_ptr<DataSenderManager> dataSenderManager,
    uint64_t persistencyUploadRetryIntervalMs,
    std::vector<std::shared_ptr<DataSenderQueue>> dataToSendQueues )
    : mDataToSendQueues( std::move( dataToSendQueues ) )
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

        size_t consumedElements = 0;
        for ( auto &queue : sender->mDataToSendQueues )
        {
            consumedElements += queue->consumeAll( [sender]( std::shared_ptr<const DataToSend> dataToSend ) {
                sender->mDataSenderManager->processData( dataToSend );
            } );
        }

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
