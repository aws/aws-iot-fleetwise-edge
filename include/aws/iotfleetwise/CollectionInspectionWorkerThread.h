// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CollectionInspectionEngine.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/TimeTypes.h"
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
    CollectionInspectionWorkerThread(
        CollectionInspectionEngine
            &collectionInspectionEngine,                 /**< The engine containing the collection inspection logic */
        std::shared_ptr<SignalBuffer> inputSignalBuffer, /**< Data sources will put signal values into this queue */
        std::shared_ptr<DataSenderQueue>
            outputCollectedData, /**< this thread will put data that should be sent to cloud into this queue */
        uint32_t idleTimeMs,     /**< if no new data is available sleep for this amount of milliseconds */
        RawData::BufferManager *rawDataBufferManager =
            nullptr /**< the raw buffer manager which is informed what data is used */
    );
    ~CollectionInspectionWorkerThread();

    CollectionInspectionWorkerThread( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread &operator=( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread( CollectionInspectionWorkerThread && ) = delete;
    CollectionInspectionWorkerThread &operator=( CollectionInspectionWorkerThread && ) = delete;

    void onChangeInspectionMatrix( std::shared_ptr<const InspectionMatrix> inspectionMatrix );

    /**
     * @brief As soon as new data is available in any input queue call this to wakeup the thread
     * */
    void onNewDataAvailable();

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

    void doWork();

    /**
     * @brief Collects data ready for the upload from collection inspection engine and passes it to the data sender
     *
     * @param waitTimeMs the amount of time to wait in ms for the next evaluation cycle
     * @return number of collected data packages successfully pushed to the upload queue
     */
    uint32_t collectDataAndUpload( uint32_t &waitTimeMs );

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
    RawData::BufferManager *mRawDataBufferManager{ nullptr };
};

} // namespace IoTFleetWise
} // namespace Aws
