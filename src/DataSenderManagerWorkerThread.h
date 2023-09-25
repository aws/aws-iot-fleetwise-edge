// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "DataSenderManager.h"
#include "IConnectivityModule.h"
#include "IDataReadyToPublishListener.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{

class DataSenderManagerWorkerThread : public IDataReadyToPublishListener
{
public:
    DataSenderManagerWorkerThread( std::shared_ptr<IConnectivityModule> connectivityModule,
                                   std::shared_ptr<DataSenderManager> dataSenderManager,
                                   uint64_t persistencyUploadRetryIntervalMs,
                                   std::shared_ptr<CollectedDataReadyToPublish> &collectedDataQueue );
    ~DataSenderManagerWorkerThread() override;

    DataSenderManagerWorkerThread( const DataSenderManagerWorkerThread & ) = delete;
    DataSenderManagerWorkerThread &operator=( const DataSenderManagerWorkerThread & ) = delete;
    DataSenderManagerWorkerThread( DataSenderManagerWorkerThread && ) = delete;
    DataSenderManagerWorkerThread &operator=( DataSenderManagerWorkerThread && ) = delete;

    static const uint32_t MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG;

    /**
     * @brief Callback from the Inspection Engine to wake up this thread and
     * publish the data to the cloud.
     */
    void onDataReadyToPublish() override;

    /**
     * @brief Stops the internal thread if started and wait until it finishes
     *
     * @return true if the stop was successful
     */
    bool stop();

    /**
     * @brief Starts the internal thread
     *
     * @return true if the start was successful
     */
    bool start();

    /**
     * @brief Checks that the worker thread is healthy and consuming data.
     */
    bool isAlive();

private:
    // Stop the  thread
    bool shouldStop() const;

    static void doWork( void *data );

    std::shared_ptr<CollectedDataReadyToPublish> mCollectedDataQueue;
    uint64_t mPersistencyUploadRetryIntervalMs{ 0 };

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Signal mWait;
    std::shared_ptr<DataSenderManager> mDataSenderManager;
    std::shared_ptr<IConnectivityModule> mConnectivityModule;

    Timer mTimer;
    Timer mRetrySendingPersistedDataTimer;
};

} // namespace IoTFleetWise
} // namespace Aws
