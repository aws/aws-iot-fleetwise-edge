// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionInspectionEngine.h"
#include "DataSenderTypes.h"
#include "RawDataManager.h"
#include "Signal.h"
#include "Thread.h"
#include "TimeTypes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{

class CollectionInspectionWorkerThread
{
public:
    CollectionInspectionWorkerThread( CollectionInspectionEngine &collectionInspectionEngine )
        : mCollectionInspectionEngine( collectionInspectionEngine ){};
    ~CollectionInspectionWorkerThread();

    CollectionInspectionWorkerThread( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread &operator=( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread( CollectionInspectionWorkerThread && ) = delete;
    CollectionInspectionWorkerThread &operator=( CollectionInspectionWorkerThread && ) = delete;

    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix );

    /**
     * @brief As soon as new data is available in any input queue call this to wakeup the thread
     * */
    void onNewDataAvailable();

    /**
     * @brief Initialize the component by handing over all queues
     *
     * @return true if initialization was successful
     * */
    bool init( const std::shared_ptr<SignalBuffer> &inputSignalBuffer, /**< IVehicleDataSourceConsumer instances will
                                                                          put relevant signals in this queue */
               const std::shared_ptr<DataSenderQueue>
                   &outputCollectedData, /**< this thread will put data that should be sent to cloud into this queue */
               uint32_t idleTimeMs,      /**< if no new data is available sleep for this amount of milliseconds */
               std::shared_ptr<RawData::BufferManager> rawBufferManager =
                   nullptr /**< the raw buffer manager which is informed what data is used */
    );

    /**
     * @brief stops the internal thread if started and wait until it finishes
     *
     * @return true if the stop was successful
     */
    bool stop();

    /**
     * @brief starts the internal thread
     *
     * @return true if the start was successful
     */
    bool start();

    /**
     * @brief Checks that the worker thread is healthy and consuming data.
     */
    bool isAlive();

private:
    static constexpr Timestamp EVALUATE_INTERVAL_MS = 1; // Evaluate every millisecond
    static constexpr uint32_t DEFAULT_THREAD_IDLE_TIME_MS = 1000;

    // Stop the  thread
    // Intercepts stop signals.
    bool shouldStop() const;

    static void doWork( void *data );

    static TimePoint calculateMonotonicTime( const TimePoint &currTime, Timestamp systemTimeMs );

    /**
     * @brief Collects data ready for the upload from collection inspection engine and passes it to the data sender
     * @return number of collected data packages successfully pushed to the upload queue
     */
    uint32_t collectDataAndUpload();

    CollectionInspectionEngine &mCollectionInspectionEngine;

    std::shared_ptr<SignalBuffer> mInputSignalBuffer;
    std::shared_ptr<DataSenderQueue> mOutputCollectedData;
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mUpdatedInspectionMatrixAvailable{ false };
    std::shared_ptr<const InspectionMatrix> mUpdatedInspectionMatrix;
    std::mutex mInspectionMatrixMutex;
    std::mutex mThreadMutex;
    Signal mWait;
    uint32_t mIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<RawData::BufferManager> mRawBufferManager{ nullptr };
};

} // namespace IoTFleetWise
} // namespace Aws
