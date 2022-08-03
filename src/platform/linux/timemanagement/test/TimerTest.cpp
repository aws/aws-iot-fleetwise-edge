
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

#include "Timer.h"

#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::Platform::Linux;

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
