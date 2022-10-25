// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
