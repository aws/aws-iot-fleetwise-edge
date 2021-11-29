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

#include "LoggingModule.h"
#include "Thread.h"
#include <atomic>

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{
using namespace Aws::IoTFleetWise::Platform;

enum class RetryStatus
{
    SUCCESS,
    RETRY,
    ABORT
};

class IRetryable
{
public:
    /**
     * @brief This function will be called for every retry
     * @return decides if the function can be retried later or succeeded or unrecoverable failed
     */
    virtual RetryStatus attempt() = 0;

    /**
     * @brief Is called after the retries stopped which means it succeeded or is aborted
     * @param code signals how it finished: if it was aborted or succeeded. retry should be never observed here
     */
    virtual void onFinished( RetryStatus code ) = 0;
    virtual ~IRetryable() = 0;
};

class RetryThread
{
public:
    RetryThread( IRetryable &retryable, uint32_t startBackoffMs, uint32_t maxBackoffMs );

    /**
     * @brief start the thread
     * @return true if thread starting was successful
     */
    bool start();

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

private:
    static void doWork( void *data );

    static std::atomic<int> fInstanceCounter;
    int fInstance;

    IRetryable &fRetryable;

    const uint32_t fStartBackoffMs;
    const uint32_t fMaxBackoffMs;

    uint32_t fCurrentWaitTime;

    LoggingModule fLogger;

    Thread fThread;
    std::atomic<bool> fShouldStop;
    std::recursive_mutex fThreadMutex;
    Signal fWait;
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws