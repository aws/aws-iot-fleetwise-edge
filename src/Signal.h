// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Wrapper on top of a condition variable. Helps Thread state transitions.
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class Signal
{
public:
    Signal() = default;
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
        auto predicate = [this]() -> bool {
            // Predicate, returns true if the wakeup signal is set.
            return mNotify;
        };
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
                // We don't use wait_for() here because there is a bug that is only fixed with GLIBC 2.30+ and GCC9+
                // - https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41861
                // - https://github.com/gcc-mirror/gcc/commit/9e68aa3cc52956ea99bb726c3c29ce0581b9f7e7
                // The reason why wait_until works is that the time is a templated type which includes the clock, so
                // the implementation will always compare against the same clock we used. For example:
                //
                // https://github.com/gcc-mirror/gcc/blob/b2d961e7342b5ba4e57adfa81cb189b738d10901/libstdc%2B%2B-v3/include/std/condition_variable#L114
                mSignalCondition.wait_until(
                    mWaitMutex, std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs ), predicate );
            }
        }

        if ( mNotify )
        {
            mNotify = false;
        }
    }

private:
    std::condition_variable mSignalCondition;
    std::atomic<bool> mNotify{ false };
    std::mutex mMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
