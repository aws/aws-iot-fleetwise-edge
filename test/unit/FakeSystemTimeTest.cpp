
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Faketime.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/Timer.h"
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <ratio>
#include <thread>

namespace Aws
{
namespace IoTFleetWise
{

using std::chrono_literals::operator""s;
using std::chrono_literals::operator""min;
using std::chrono_literals::operator""h;

TEST( TimerTest, timerTickCountChangingSystemTimeBackward )
{
    Timer timer;
    Faketime faketime( Faketime::SYSTEM_ONLY );
    auto clock = ClockHandler::getClock();

    auto systemTimeBeforeChange = clock->systemTimeSinceEpochMs();
    faketime.setTime( "-2h" );
    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_GE( clock->systemTimeSinceEpochMs(), systemTimeBeforeChange - std::chrono::milliseconds( 2h ).count() );
    ASSERT_LT( clock->systemTimeSinceEpochMs(), systemTimeBeforeChange - std::chrono::milliseconds( 118min ).count() );

    ASSERT_GE( timer.getElapsedMs().count(), 0LU );
    ASSERT_GE( timer.getElapsedSeconds(), 0 );
}

TEST( TimerTest, timerTickCountChangingSystemTimeForward )
{
    Timer timer;
    Faketime faketime( Faketime::SYSTEM_ONLY );
    auto clock = ClockHandler::getClock();

    auto systemTimeBeforeChange = clock->systemTimeSinceEpochMs();
    faketime.setTime( "+2h" );
    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_GE( clock->systemTimeSinceEpochMs(), systemTimeBeforeChange + std::chrono::milliseconds( 2h ).count() );
    ASSERT_LT( clock->systemTimeSinceEpochMs(), systemTimeBeforeChange + std::chrono::milliseconds( 122min ).count() );

    ASSERT_GE( timer.getElapsedMs().count(), 0LU );
    // We moved 2 hours in the future, but let's be safe and check that elapsed time is much less than the jump
    ASSERT_LT( timer.getElapsedMs().count(), std::chrono::milliseconds( 1h ).count() );
    ASSERT_GE( timer.getElapsedSeconds(), 0 );
}

class SignalTest : public ::testing::Test
{
protected:
    SignalTest()
        : mFaketime( Faketime::SYSTEM_ONLY )
    {
    }

    Signal mSignal;
    Timer mTimer;
    Faketime mFaketime;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

TEST_F( SignalTest, waitWithTimeoutChangingSystemTimeBackward )
{
    auto systemTimeBeforeChange = mClock->systemTimeSinceEpochMs();
    auto timeTolerance = std::chrono::milliseconds( 200 );
    auto timeChangeDelay = std::chrono::milliseconds( 500 );
    // We will change the system time after some delay so that it is changed after the wait method below is
    // already executing.
    std::thread changeSystemTime( [&]() {
        std::this_thread::sleep_for( timeChangeDelay );
        mFaketime.setTime( "-30s" );
    } );
    auto timeout = std::chrono::milliseconds( 1000 );
    mSignal.wait( static_cast<uint32_t>( timeout.count() ) );
    changeSystemTime.join();
    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_GE( mClock->systemTimeSinceEpochMs(), systemTimeBeforeChange - std::chrono::milliseconds( 30s ).count() );
    ASSERT_LT( mClock->systemTimeSinceEpochMs(), systemTimeBeforeChange - std::chrono::milliseconds( 20s ).count() );

    // If the condition_variable is behaving correctly, it should not take much more nor much less than the timeout.
    // If this fails it probably means that the timeout logic is not using a monotonic clock.
    ASSERT_GT( mTimer.getElapsedMs().count(), ( timeout - timeTolerance ).count() );
    ASSERT_LT( mTimer.getElapsedMs().count(), ( timeout + timeTolerance ).count() );
}

TEST_F( SignalTest, waitWithTimeoutChangingSystemTimeForwardWhileWaiting )
{
    auto systemTimeBeforeChange = mClock->systemTimeSinceEpochMs();
    auto timeTolerance = std::chrono::milliseconds( 200 );
    auto timeChangeDelay = std::chrono::milliseconds( 500 );
    // We will change the system time after some delay so that it is changed after the wait method below is
    // already executing.
    std::thread changeSystemTime( [&]() {
        std::this_thread::sleep_for( timeChangeDelay );
        mFaketime.setTime( "+30s" );
    } );
    auto timeout = std::chrono::milliseconds( 1000 );
    mSignal.wait( static_cast<uint32_t>( timeout.count() ) );
    changeSystemTime.join();

    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_GE( mClock->systemTimeSinceEpochMs(), systemTimeBeforeChange + std::chrono::milliseconds( 30s ).count() );
    ASSERT_LT( mClock->systemTimeSinceEpochMs(), systemTimeBeforeChange + std::chrono::milliseconds( 40s ).count() );

    // If the condition_variable is behaving correctly, it should not take much more nor much less than the timeout.
    // If this fails it probably means that the timeout logic is not using a monotonic clock.
    ASSERT_GT( mTimer.getElapsedMs().count(), ( timeout - timeTolerance ).count() );
    ASSERT_LT( mTimer.getElapsedMs().count(), ( timeout + timeTolerance ).count() );
}

TEST_F( SignalTest, waitWithTimeoutChangingSystemTimeForwardBeforeWaiting )
{
    auto systemTimeBeforeChange = mClock->systemTimeSinceEpochMs();
    auto timeTolerance = std::chrono::milliseconds( 200 );
    auto timeChangeDelay = std::chrono::milliseconds( 500 );
    mFaketime.setTime( "+30s" );
    // Sanity check to ensure faketime is working. If this fails, it means faketime is not properly set up.
    ASSERT_GE( mClock->systemTimeSinceEpochMs(), systemTimeBeforeChange + std::chrono::milliseconds( 30s ).count() );
    ASSERT_LT( mClock->systemTimeSinceEpochMs(), systemTimeBeforeChange + std::chrono::milliseconds( 40s ).count() );

    auto timeout = std::chrono::milliseconds( 1000 );
    mSignal.wait( static_cast<uint32_t>( timeout.count() ) );

    // If the condition_variable is behaving correctly, it should not take much more nor much less than the timeout.
    // If this fails it probably means that the timeout logic is not using a monotonic clock.
    ASSERT_GT( mTimer.getElapsedMs().count(), ( timeout - timeTolerance ).count() );
    ASSERT_LT( mTimer.getElapsedMs().count(), ( timeout + timeTolerance ).count() );
}

} // namespace IoTFleetWise
} // namespace Aws
