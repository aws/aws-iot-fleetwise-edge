// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CommandTypes.h"
#include "DataSenderTypes.h"
#include "LastKnownStateInspector.h"
#include "LastKnownStateTypes.h"
#include "Signal.h"
#include "Thread.h"
#include "TimeTypes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This thread retrieves and inspects data for the Last Known State data collection
 * based on the state templates and queues collected data for the upload
 */
class LastKnownStateWorkerThread
{
public:
    LastKnownStateWorkerThread( std::shared_ptr<SignalBuffer> inputSignalBuffer,
                                std::shared_ptr<DataSenderQueue> collectedLastKnownStateData,
                                std::unique_ptr<LastKnownStateInspector> lastKnownStateInspector,
                                uint32_t idleTimeMs );
    ~LastKnownStateWorkerThread();

    LastKnownStateWorkerThread( const LastKnownStateWorkerThread & ) = delete;
    LastKnownStateWorkerThread &operator=( const LastKnownStateWorkerThread & ) = delete;
    LastKnownStateWorkerThread( LastKnownStateWorkerThread && ) = delete;
    LastKnownStateWorkerThread &operator=( LastKnownStateWorkerThread && ) = delete;

    /**
     * @brief Callback to notify when there is new data available
     */
    void onNewDataAvailable();

    /**
     * @brief Callback to notify that the new state templates are available
     */
    void onStateTemplatesChanged( std::shared_ptr<StateTemplateList> stateTemplates );

    /**
     * @brief Handle a new LastKnownState command
     */
    void onNewCommandReceived( const LastKnownStateCommandRequest &commandRequest );

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
    static constexpr uint32_t DEFAULT_THREAD_IDLE_TIME_MS = 1000;

    // Stop the  thread
    // Intercepts stop signals.
    bool shouldStop() const;

    static void doWork( void *data );

    static TimePoint calculateMonotonicTime( const TimePoint &currTime, Timestamp systemTimeMs );

    std::shared_ptr<SignalBuffer> mInputSignalBuffer;
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Signal mWait;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    uint32_t mIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };

    bool mStateTemplatesAvailable{ false };
    std::shared_ptr<StateTemplateList> mStateTemplatesInput;
    std::shared_ptr<StateTemplateList> mStateTemplates;
    std::mutex mStateTemplatesMutex;
    std::shared_ptr<DataSenderQueue> mCollectedLastKnownStateData;

    std::vector<LastKnownStateCommandRequest> mLastKnownStateCommandsInput;
    std::mutex mLastKnownStateCommandsMutex;

    std::unique_ptr<LastKnownStateInspector> mLastKnownStateInspector;
};

} // namespace IoTFleetWise
} // namespace Aws
