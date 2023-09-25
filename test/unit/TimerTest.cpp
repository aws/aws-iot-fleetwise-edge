// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Timer.h"
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{

TEST( TimerTest, timerState )
{
    Timer timer;
    ASSERT_TRUE( timer.isTimerRunning() );
    timer.pause();
    ASSERT_FALSE( timer.isTimerRunning() );
    timer.resume();
    ASSERT_TRUE( timer.isTimerRunning() );
    timer.reset();
    ASSERT_TRUE( timer.isTimerRunning() );
}

TEST( TimerTest, timerTickCount )
{
    Timer timer;
    ASSERT_TRUE( timer.isTimerRunning() );
    ASSERT_GE( timer.getElapsedMs().count(), 0LU );
    ASSERT_GE( timer.getElapsedSeconds(), 0 );
}

} // namespace IoTFleetWise
} // namespace Aws
