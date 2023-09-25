// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "WaitUntil.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

TEST( TimerTest, waitUntilTest )
{
    // A determent function with negative return, it will retry for `WAIT_TIME_OUT`
    auto startTime = std::chrono::steady_clock::now();
    waitUntil( [] {
        return 1 == 2;
    } );
    auto elapsedTime = std::chrono::steady_clock::now() - startTime;
    ASSERT_LT( WAIT_TIME_OUT, elapsedTime );

    // A determent function with positive return, it won't retry or wait for `WAIT_TIME_OUT`
    startTime = std::chrono::steady_clock::now();
    waitUntil( [] {
        return 1 == 1;
    } );
    elapsedTime = std::chrono::steady_clock::now() - startTime;
    ASSERT_GT( WAIT_TIME_OUT, elapsedTime );
}

TEST( TimerTest, waitAssertEQ )
{
    int val_1 = 1;
    int val_expected = 2;
    std::thread changeVal( [&val_1, val_expected]() {
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        val_1 = val_expected;
    } );
    changeVal.join();
    WAIT_ASSERT_EQ( val_1, val_expected );
}

TEST( TimerTest, expectTwoDiffValueTestToFail )
{
    int val_1 = 1;
    int val_expected = 2;
    GTEST_SKIP() << "Skipping this single test"; // comment out when test locally if necessary
    WAIT_ASSERT_EQ( val_1, val_expected );
}

TEST( TimerTest, waitAssertTrue )
{
    std::vector<int> signal{ 1 };
    WAIT_ASSERT_TRUE( !signal.empty() );
}

TEST( TimerTest, delayAssertTrue )
{
    int i = 100;
    auto countdown = [&] {
        if ( i == 0 )
        {
            return true;
        }
        i--;
        return false;
    };
    DELAY_ASSERT_TRUE( countdown() );
}

TEST( TimerTest, delayAssertFalse )
{
    int i = 100;
    auto countdown = [&] {
        if ( i == 0 )
        {
            return false;
        }
        i--;
        return true;
    };
    DELAY_ASSERT_FALSE( countdown() );
}

} // namespace IoTFleetWise
} // namespace Aws
