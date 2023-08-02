// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataSenderManager.h"
#include "IActiveCollectionSchemesListener.h"
#include "IConnectivityModule.h"
#include "IDataReadyToPublishListener.h"
#include "Thread.h"
#include "Timer.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataSender
{

class DataSenderManagerWorkerThread : public IDataReadyToPublishListener, public IActiveCollectionSchemesListener
{
public:
    DataSenderManagerWorkerThread( CANInterfaceIDTranslator &canIDTranslator,
                                   std::shared_ptr<IConnectivityModule> connectivityModule,
                                   std::shared_ptr<ISender> mqttSender,
                                   std::shared_ptr<PayloadManager> payloadManager,
                                   unsigned maxMessageCount,
                                   uint64_t persistencyUploadRetryInterval );
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

    // Inherited from IActiveConditionProcessor
    void onChangeCollectionSchemeList(
        const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes ) override;

    /**
     * @brief Initialize the component by handing over all queues
     * @param collectedDataQueue this thread will empty this queue of data ready to publish
     * @return true if initialization was successful
     * */
    bool init( const std::shared_ptr<CollectedDataReadyToPublish> &collectedDataQueue );

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

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Platform::Linux::Signal mWait;
    DataSenderManager mDataSenderManager;
    std::shared_ptr<const ActiveCollectionSchemes> mActiveCollectionSchemes;
    std::mutex mActiveCollectionSchemesMutex;
    std::atomic<bool> mUpdatedCollectionSchemeListAvailable{ false };

    std::shared_ptr<IConnectivityModule> mConnectivityModule;

    Timer mTimer;
    Timer mRetrySendingPersistedDataTimer;
    uint64_t mPersistencyUploadRetryIntervalMs{ 0 };
};

} // namespace DataSender
} // namespace IoTFleetWise
} // namespace Aws
