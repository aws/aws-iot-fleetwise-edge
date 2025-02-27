// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/DataSenderManager.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/IConnectivityModule.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/Timer.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class DataSenderManagerWorkerThread
{
public:
    DataSenderManagerWorkerThread( const IConnectivityModule &connectivityModule,
                                   std::unique_ptr<DataSenderManager> dataSenderManager,
                                   uint64_t persistencyUploadRetryIntervalMs,
                                   std::vector<std::shared_ptr<DataSenderQueue>> dataToSendQueues );
    ~DataSenderManagerWorkerThread();

    DataSenderManagerWorkerThread( const DataSenderManagerWorkerThread & ) = delete;
    DataSenderManagerWorkerThread &operator=( const DataSenderManagerWorkerThread & ) = delete;
    DataSenderManagerWorkerThread( DataSenderManagerWorkerThread && ) = delete;
    DataSenderManagerWorkerThread &operator=( DataSenderManagerWorkerThread && ) = delete;

    static const uint32_t MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG;

    /**
     * @brief Callback from the Inspection Engine to wake up this thread and
     * publish the data to the cloud.
     */
    void onDataReadyToPublish();

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

    void doWork();

    std::vector<std::shared_ptr<DataSenderQueue>> mDataToSendQueues;
    uint64_t mPersistencyUploadRetryIntervalMs{ 0 };

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Signal mWait;
    std::unique_ptr<DataSenderManager> mDataSenderManager;
    const IConnectivityModule &mConnectivityModule;

    Timer mTimer;
    Timer mRetrySendingPersistedDataTimer;
};

} // namespace IoTFleetWise
} // namespace Aws
