/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "Signal.h"
#include <assert.h>
#include <functional>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
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

    Thread();

    ~Thread();

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
    void setThreadName( const std::string &name );

    /**
     * @brief Sets Thread name of the callee thread
     */
    static void SetCurrentThreadName( const std::string &name );

private:
    struct ThreadSettings
    {
        ThreadSettings()
            : mSelf( nullptr )
            , mWorkerFunction( nullptr )
            , mParams( nullptr )
        {
        }

        Thread *mSelf;
        WorkerFunction mWorkerFunction;
        void *mParams;
    };
    static void *workerFunctionWrapper( void *params );

    unsigned long getThreadID();

    pthread_t mThread;
    ThreadSettings mExecParams;

    unsigned long mThreadId{};

    std::atomic_bool mDone{ false };

    std::unique_ptr<Signal> mTerminateSignal;
};

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX