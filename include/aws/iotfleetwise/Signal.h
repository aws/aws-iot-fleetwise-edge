// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Assert.h"
#include <atomic>
#include <boost/thread.hpp>
#include <chrono>
#include <memory>
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
    Signal()
    {
        try
        {
            mSignalCondition = std::make_unique<boost::condition_variable>();
        }
        catch ( boost::thread_resource_error &e )
        {
            FWE_FATAL_ASSERT( false, "Error while creating condition variable: " + std::string( e.what() ) );
        }
    };
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
        try
        {
            boost::unique_lock<boost::mutex> mWaitMutex( mMutex );
            mNotify = true;
            mWaitMutex.unlock();
            mSignalCondition->notify_one();
        }
        catch ( boost::lock_error &e )
        {
            // a lock exception means there is something really wrong in the way we are using it
            FWE_FATAL_ASSERT( false, "Error while locking the mutex: " + std::string( e.what() ) );
        }
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

        try
        {
            boost::unique_lock<boost::mutex> mWaitMutex( mMutex );
            if ( !predicate() )
            {
                if ( timeoutMs == WaitWithPredicate )
                {
                    // Waits until the predicate returns true
                    mSignalCondition->wait( mWaitMutex, predicate );
                }
                else
                {
                    // Waits until either the predicate returns true or the timeout expires.
                    // We are using boost::condition_variable here because there are few issues with
                    // std::condition_variable in older versions of GLIBC, GCC and pthread:
                    //
                    // 1. std::condition_variable::wait_for doesn't use the steady clock (monotonic). This has been
                    // fixed in GLIBC 2.30+ and GCC9+.
                    //
                    // - https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41861
                    // - https://github.com/gcc-mirror/gcc/commit/9e68aa3cc52956ea99bb726c3c29ce0581b9f7e7
                    //
                    // 2. An workaround for the first bug would be to use wait_until(steady_clock::now() + milliseconds(
                    // timeoutMs ), which is equivalent to wait_for. But that doesn't work in all situations. When the
                    // time jumps to the future while the function is waiting, it will wait much longer than expected
                    // because the underlying implementation uses a pthread function that waits based on system time.
                    // This has been fully fixed in GCC 10+:
                    //
                    // - https://github.com/gcc-mirror/gcc/commit/ad4d1d21ad5c515ba90355d13b14cbb74262edd2
                    //
                    // Boost's implementation doesn't wait longer than necessary when time jumps, but there can still be
                    // spurious wake-ups. In case that happens we just wait again until the desired time elapses.
                    auto beforeWait = std::chrono::steady_clock::now();
                    while ( !predicate() )
                    {
                        mSignalCondition->wait_for( mWaitMutex, boost::chrono::milliseconds( timeoutMs ), predicate );
                        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - beforeWait );
                        if ( elapsedMs.count() >= timeoutMs )
                        {
                            break;
                        }
                    }
                }
            }

            if ( mNotify )
            {
                mNotify = false;
            }
        }
        catch ( boost::lock_error &e )
        {
            // a lock exception means there is something really wrong in the way we are using it
            FWE_FATAL_ASSERT( false, "Error while locking the mutex: " + std::string( e.what() ) );
        }
        catch ( boost::thread_interrupted & )
        {
            // We don't really use boost threads or interrupts, so this shouldn't happen.
            FWE_FATAL_ASSERT( false,
                              "boost::thread_interrupted thrown while trying to call wait() on condition variable" );
        }
    }

private:
    std::unique_ptr<boost::condition_variable> mSignalCondition;
    std::atomic<bool> mNotify{ false };
    boost::mutex mMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
