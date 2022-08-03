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

// Includes

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{
/**
 * @brief Wrapper on top of a condition variable. Helps Thread state transitions.
 */
class Signal
{
public:
    Signal()
    {
        mNotify = false;
    }
    ~Signal() = default;

    /**
     * @brief wait constant passed to the condition variable. Releases when the
     * predicate returns true.
     */
    static constexpr uint32_t WaitWithPredicate = static_cast<uint32_t>( -1 );
    /**
     * @brief Wakes up the thread from the waiting cycle.
     */
    void
    notify()
    {
        std::unique_lock<std::mutex> mWaitMutex( mMutex );
        mNotify = true;
        mWaitMutex.unlock();
        mSignalCondition.notify_one();
    }

    /**
     * @brief Wait for some time for the signal to be set.
     * @param timeoutMs timeout in Milliseconds.
     */
    void
    wait( uint32_t timeoutMs )
    {
        // Predicate, returns true if the wakeup signal is set.
        auto predicate = [this]() -> bool { return mNotify; };
        std::unique_lock<std::mutex> mWaitMutex( mMutex );
        if ( !predicate() )
        {
            if ( timeoutMs == WaitWithPredicate )
            {
                // Waits until the predicate returns true
                mSignalCondition.wait( mWaitMutex, predicate );
            }
            else
            {
                // Waits until either the predicate returns true or the timeout expires.
                mSignalCondition.wait_for( mWaitMutex, std::chrono::milliseconds( timeoutMs ), predicate );
            }
        }

        if ( mNotify )
        {
            mNotify = false;
        }
    }

private:
    std::condition_variable mSignalCondition;
    std::atomic<bool> mNotify;
    std::mutex mMutex;
};

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
