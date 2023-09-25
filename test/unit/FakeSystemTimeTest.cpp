
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Faketime.h"
#include "Signal.h"
#include "Timer.h"
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <ratio>
#include <thread>

namespace Aws
{
namespace IoTFleetWise
{

using std::chrono_literals::operator""h;

TEST( TimerTest, timerTickCountChangingSystemTimeBackward )
{
    Timer timer;
    Faketime faketime( Faketime::SYSTEM_ONLY );

    auto systemTimeBeforeChange = std::chrono::system_clock::now().time_since_epoch().count();
    faketime.setTime( "-2h" );
    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_LT( std::chrono::system_clock::now().time_since_epoch().count(), systemTimeBeforeChange );

    ASSERT_GE( timer.getElapsedMs().count(), 0LU );
    ASSERT_GE( timer.getElapsedSeconds(), 0 );
}

TEST( TimerTest, timerTickCountChangingSystemTimeForward )
{
    Timer timer;
    Faketime faketime( Faketime::SYSTEM_ONLY );

    auto systemTimeBeforeChange = std::chrono::system_clock::now().time_since_epoch().count();
    faketime.setTime( "+2h" );
    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_GT( std::chrono::system_clock::now().time_since_epoch().count(), systemTimeBeforeChange );

    ASSERT_GE( timer.getElapsedMs().count(), 0LU );
    // We moved 2 hours in the future, but let's be safe and check that elapsed time is much less than the jump
    ASSERT_LT( timer.getElapsedMs().count(), std::chrono::milliseconds( 1h ).count() );
    ASSERT_GE( timer.getElapsedSeconds(), 0 );
}

TEST( SignalTest, waitWithTimeoutChangingSystemTimeBackward )
{
    Signal signal;
    Timer timer;
    Faketime faketime( Faketime::SYSTEM_ONLY );

    auto systemTimeBeforeChange = std::chrono::system_clock::now().time_since_epoch().count();
    auto timeMargin = std::chrono::milliseconds( 500 );
    // We will change the system time after some delay so that it is changed after the wait method below is
    // already executing.
    std::thread changeSystemTime( [&faketime, timeMargin]() {
        std::this_thread::sleep_for( timeMargin );
        faketime.setTime( "-30s" );
    } );
    auto timeout = std::chrono::milliseconds( 1000 );
    signal.wait( static_cast<uint32_t>( timeout.count() ) );
    changeSystemTime.join();

    // If the condition is behaving correctly, it should not take much more nor much less than the timeout.
    // If this fails it probably means that the timeout logic is not using a monotonic clock.
    ASSERT_GT( timer.getElapsedMs().count(), ( timeout - timeMargin ).count() );
    ASSERT_LT( timer.getElapsedMs().count(), ( timeout + timeMargin ).count() );

    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_LT( std::chrono::system_clock::now().time_since_epoch().count(), systemTimeBeforeChange );
}

} // namespace IoTFleetWise
} // namespace Aws
