// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Signal.h"
#include <atomic>
#include <functional>
#include <memory>
#include <pthread.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief POSIX Thread Wrapper Implementation.
 * As a rule of thumb, Thread names must follow this convention i.e.
 * [fw] -- short name of IoTFleetWise (lower case)
 * [MN] -- short name of Module Name e.g. DI for Data Inspection
 * [UnitName] --- Name of the Unit where the thread is created e.g. Consumer
 * [ThreadIndex] --- The thread index if it's part of a pool e.g. Consumer1. starting from index 1
 * PS: Name should not exceed 15 Characters, to make sure we can deal with other OSs such as QNX
 */
class Thread
{
public:
    using WorkerFunction = std::function<void( void * )>;

    /**
     * @brief Creates a POSIX Thread.
     * @param workerFunction function pointer to the thread task.
     * @param execParam thread handle execution settings.
     * @return True if the thread has been created.
     */
    bool create( WorkerFunction workerFunction, void *execParam );

    /**
     * @brief Checks if the thread is actively running
     * @return True if it's active.
     */
    bool isActive() const;

    /**
     * @brief Checks if the thread is valid
     * @return True if it's valid.
     */
    bool isValid() const;

    /**
     * @brief Release the thread i.e. terminate
     * @return True if it's successful.
     */
    bool release();

    /**
     * @brief Sets Thread name
     */
    // NOLINTNEXTLINE(readability-make-member-function-const)
    void setThreadName( const std::string &name );

    /**
     * @brief Sets Thread name of the callee thread
     */
    static void setCurrentThreadName( const std::string &name );

    // callback called from thread implementation library like pthread
    static void *workerFunctionWrapper( void *params );

private:
    struct ThreadSettings
    {
        Thread *mSelf{ nullptr };
        WorkerFunction mWorkerFunction{ nullptr };
        void *mParams{ nullptr };
    };

    unsigned long getThreadID() const;

    pthread_t mThread{ 0U };
    ThreadSettings mExecParams;

    unsigned long mThreadId{};

    std::atomic_bool mDone{ false };

    std::unique_ptr<Signal> mTerminateSignal;
};

} // namespace IoTFleetWise
} // namespace Aws
