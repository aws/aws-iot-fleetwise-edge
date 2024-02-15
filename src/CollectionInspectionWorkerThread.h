// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionInspectionEngine.h"
#include "Listener.h"
#include "Signal.h"
#include "Thread.h"
#include "TimeTypes.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "RawDataManager.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

class CollectionInspectionWorkerThread
{
public:
    using OnDataReadyToPublishCallback = std::function<void()>;

    CollectionInspectionWorkerThread() = default;
    ~CollectionInspectionWorkerThread();

    CollectionInspectionWorkerThread( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread &operator=( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread( CollectionInspectionWorkerThread && ) = delete;
    CollectionInspectionWorkerThread &operator=( CollectionInspectionWorkerThread && ) = delete;

    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix );

    /**
     * @brief Register a callback to be called when data is ready to be published to the cloud
     * */
    void
    subscribeToDataReadyToPublish( OnDataReadyToPublishCallback callback )
    {
        mDataReadyListeners.subscribe( callback );
    }

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
               const std::shared_ptr<CollectedDataReadyToPublish>
                   &outputCollectedData, /**< this thread will put data that should be sent to cloud into this queue */
               uint32_t idleTimeMs       /**< if no new data is available sleep for this amount of milliseconds */
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
               ,
               std::shared_ptr<RawData::BufferManager> rawBufferManager =
                   nullptr /**< the raw buffer manager which is informed what data is used */
#endif
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

    CollectionInspectionEngine fCollectionInspectionEngine;

    std::shared_ptr<SignalBuffer> fInputSignalBuffer;
    std::shared_ptr<CollectedDataReadyToPublish> fOutputCollectedData;
    Thread fThread;
    std::atomic<bool> fShouldStop{ false };
    std::atomic<bool> fUpdatedInspectionMatrixAvailable{ false };
    std::shared_ptr<const InspectionMatrix> fUpdatedInspectionMatrix;
    std::mutex fInspectionMatrixMutex;
    std::mutex fThreadMutex;
    Signal fWait;
    uint32_t fIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };
    std::shared_ptr<const Clock> fClock = ClockHandler::getClock();
    ThreadSafeListeners<OnDataReadyToPublishCallback> mDataReadyListeners;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::shared_ptr<RawData::BufferManager> mRawBufferManager{ nullptr };
#endif
};

} // namespace IoTFleetWise
} // namespace Aws
