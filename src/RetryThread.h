// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Signal.h"
#include "Thread.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{

enum class RetryStatus
{
    SUCCESS,
    RETRY,
    ABORT
};

/**
 * @brief This function will be called for every retry
 * @return decides if the function can be retried later or succeeded or unrecoverable failed
 */
using Retryable = std::function<RetryStatus()>;

class RetryThread
{
public:
    RetryThread( Retryable retryable, uint32_t startBackoffMs, uint32_t maxBackoffMs );

    /**
     * @brief start the thread
     * @return true if thread starting was successful
     */
    bool start();

    /**
     * @brief Restart the thread
     *
     * If the retryable is currently running, it won't be stopped, but the thread will run it again
     * even if the retryable succeeds.
     * The time to wait between retries is also reset.
     */
    void restart();

    /**
     * @brief stops the thread
     * @return true if thread stopping was successful
     */
    bool stop();

    /**
     * @brief check if thread is currently running
     * @return true if thread is currently running
     */
    bool
    isAlive()
    {
        return fThread.isValid() && fThread.isActive();
    }

    ~RetryThread()
    {
        // To make sure the thread stops during teardown of tests.
        if ( isAlive() )
        {
            stop();
        }
    }

    RetryThread( const RetryThread & ) = delete;
    RetryThread &operator=( const RetryThread & ) = delete;
    RetryThread( RetryThread && ) = delete;
    RetryThread &operator=( RetryThread && ) = delete;

private:
    static void doWork( void *data );

    static std::atomic<int> fInstanceCounter; // NOLINT Global atomic instance counter
    Retryable fRetryable;
    int fInstance;
    const uint32_t fStartBackoffMs;
    const uint32_t fMaxBackoffMs;
    uint32_t fCurrentWaitTime;
    Thread fThread;
    std::atomic<bool> fShouldStop;
    std::atomic<bool> fRestart;
    std::mutex fThreadMutex;
    Signal fWait;
};

} // namespace IoTFleetWise
} // namespace Aws
