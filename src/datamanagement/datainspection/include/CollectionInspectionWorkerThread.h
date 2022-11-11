// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ClockHandler.h"
#include "CollectionInspectionEngine.h"
#include "IDataReadyToPublishListener.h"
#include "Listener.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::DataManagement;
using Aws::IoTFleetWise::Platform::Linux::ThreadListeners;

class CollectionInspectionWorkerThread : public IActiveConditionProcessor,
                                         public ThreadListeners<IDataReadyToPublishListener>
{
public:
    CollectionInspectionWorkerThread() = default;
    ~CollectionInspectionWorkerThread() override;

    CollectionInspectionWorkerThread( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread &operator=( const CollectionInspectionWorkerThread & ) = delete;
    CollectionInspectionWorkerThread( CollectionInspectionWorkerThread && ) = delete;
    CollectionInspectionWorkerThread &operator=( CollectionInspectionWorkerThread && ) = delete;

    // Inherited from IActiveConditionProcessor
    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &activeConditions ) override;

    /**
     * @brief As soon as new data is available in any input queue call this to wakeup the thread
     * */
    void onNewDataAvailable();

    /**
     * @brief Initialize the component by handing over all queues
     * @param inputSignalBuffer IVehicleDataSourceConsumer instances will put relevant signals in this queue
     * @param inputCANBuffer CANDataConsumers will put relevant raw can frames in this queue
     * @param inputActiveDTCBuffer OBDModule DTC Circular buffer
     * @param outputCollectedData this thread will put data that should be sent to cloud into this queue
     * @param idleTimeMs if no new data is available sleep for this amount of milliseconds
     * @param dataReductionProbability set to true to disable data reduction using probability
     *
     * @return true if initialization was successful
     * */
    bool init( const std::shared_ptr<SignalBuffer> &inputSignalBuffer,
               const std::shared_ptr<CANBuffer> &inputCANBuffer,
               const std::shared_ptr<ActiveDTCBuffer> &inputActiveDTCBuffer,
               const std::shared_ptr<CollectedDataReadyToPublish> &outputCollectedData,
               uint32_t idleTimeMs,
               bool dataReductionProbability = false );

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

    /**
     * @brief Register a thread as a listener to the Inspection Engine events.
     * Thread safety is guaranteed by the underlying ThreadListener instance
     * @param listener an InspectionEventListener instance
     * @return the outcome of the Listener interface subscribeListener()
     */
    inline bool
    subscribeToEvents( InspectionEventListener *listener )
    {
        return fCollectionInspectionEngine.subscribeListener( listener );
    }

    /**
     * @brief unRegister a thread as a listener from the Inspection Engine events.
     * Thread safety is guaranteed by the underlying ThreadListener instance
     * @param listener an InspectionEventListener instance
     * @return the outcome of the Listener interface unSubscribeListener()
     */
    inline bool
    unSubscribeFromEvents( InspectionEventListener *listener )
    {
        return fCollectionInspectionEngine.unSubscribeListener( listener );
    }

private:
    static constexpr Timestamp EVALUATE_INTERVAL_MS = 1; // Evaluate every millisecond
    static constexpr uint32_t DEFAULT_THREAD_IDLE_TIME_MS = 1000;

    // Stop the  thread
    // Intercepts stop signals.
    bool shouldStop() const;

    static void doWork( void *data );

    CollectionInspectionEngine fCollectionInspectionEngine;

    std::shared_ptr<SignalBuffer> fInputSignalBuffer;
    std::shared_ptr<CANBuffer> fInputCANBuffer;
    std::shared_ptr<ActiveDTCBuffer> fInputActiveDTCBuffer;
    std::shared_ptr<CollectedDataReadyToPublish> fOutputCollectedData;
    Thread fThread;
    std::atomic<bool> fShouldStop{ false };
    std::atomic<bool> fUpdatedInspectionMatrixAvailable{ false };
    std::shared_ptr<const InspectionMatrix> fUpdatedInspectionMatrix;
    std::mutex fInspectionMatrixMutex;
    std::mutex fThreadMutex;
    Platform::Linux::Signal fWait;
    LoggingModule fLogger;
    uint32_t fIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };
    std::shared_ptr<const Clock> fClock = ClockHandler::getClock();
};

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws