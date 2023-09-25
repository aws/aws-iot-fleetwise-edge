// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#define WAIT_VAR_EXPAND( x, y ) x##y
#define WAIT_VAR( x, y ) WAIT_VAR_EXPAND( x, y )

/*
  The following WAIT_ASSERT_* are adding extra retrial capability above the GoogleTest framework.
  Some test cases need longer time to process before get expected outcome, so we need to wait and retry.
*/
#define WAIT_ASSERT_TRUE( val )                                                                                        \
    ASSERT_TRUE( waitUntil( [&] {                                                                                      \
        return ( val );                                                                                                \
    } ) )
#define WAIT_ASSERT_FALSE( val )                                                                                       \
    ASSERT_TRUE( waitUntil( [&] {                                                                                      \
        return !( val );                                                                                               \
    } ) )

#define WAIT_ASSERT_EQ( val1, val2 )                                                                                   \
    auto WAIT_VAR( val1_, __LINE__ ) = val1;                                                                           \
    auto WAIT_VAR( val2_, __LINE__ ) = val2;                                                                           \
    if ( WAIT_VAR( val1_, __LINE__ ) != WAIT_VAR( val2_, __LINE__ ) )                                                  \
    {                                                                                                                  \
        waitUntil( [&] {                                                                                               \
            WAIT_VAR( val1_, __LINE__ ) = val1;                                                                        \
            WAIT_VAR( val2_, __LINE__ ) = val2;                                                                        \
            return WAIT_VAR( val1_, __LINE__ ) == WAIT_VAR( val2_, __LINE__ );                                         \
        } );                                                                                                           \
    }                                                                                                                  \
    ASSERT_EQ( WAIT_VAR( val1_, __LINE__ ), WAIT_VAR( val2_, __LINE__ ) ) << "where val1 is " #val1 ", val2 is " #val2

#define WAIT_ASSERT_GT( val1, val2 )                                                                                   \
    auto WAIT_VAR( val1_, __LINE__ ) = val1;                                                                           \
    auto WAIT_VAR( val2_, __LINE__ ) = val2;                                                                           \
    if ( WAIT_VAR( val1_, __LINE__ ) <= WAIT_VAR( val2_, __LINE__ ) )                                                  \
    {                                                                                                                  \
        waitUntil( [&] {                                                                                               \
            WAIT_VAR( val1_, __LINE__ ) = val1;                                                                        \
            WAIT_VAR( val2_, __LINE__ ) = val2;                                                                        \
            return WAIT_VAR( val1_, __LINE__ ) > WAIT_VAR( val2_, __LINE__ );                                          \
        } );                                                                                                           \
    }                                                                                                                  \
    ASSERT_GT( WAIT_VAR( val1_, __LINE__ ), WAIT_VAR( val2_, __LINE__ ) ) << "where val1 is " #val1 ", val2 is " #val2

#define WAIT_ASSERT_LT( val1, val2 )                                                                                   \
    auto WAIT_VAR( val1_, __LINE__ ) = val1;                                                                           \
    auto WAIT_VAR( val2_, __LINE__ ) = val2;                                                                           \
    if ( WAIT_VAR( val1_, __LINE__ ) >= WAIT_VAR( val2_, __LINE__ ) )                                                  \
    {                                                                                                                  \
        waitUntil( [&] {                                                                                               \
            WAIT_VAR( val1_, __LINE__ ) = val1;                                                                        \
            WAIT_VAR( val2_, __LINE__ ) = val2;                                                                        \
            return WAIT_VAR( val1_, __LINE__ ) < WAIT_VAR( val2_, __LINE__ );                                          \
        } );                                                                                                           \
    }                                                                                                                  \
    ASSERT_LT( WAIT_VAR( val1_, __LINE__ ), WAIT_VAR( val2_, __LINE__ ) ) << "where val1 is " #val1 ", val2 is " #val2

/*
  The following DELAYED_ASSERT_* are adding extra retrial capability above the GoogleTest framework.
  Some test cases need to be continuously evaluated, and asserted only after the timeout occurs.
*/
#define DELAY_ASSERT_TRUE( val )                                                                                       \
    ASSERT_TRUE( waitDelay( [&] {                                                                                      \
        return ( val );                                                                                                \
    } ) )
#define DELAY_ASSERT_FALSE( val )                                                                                      \
    ASSERT_FALSE( waitDelay( [&] {                                                                                     \
        return ( val );                                                                                                \
    } ) )

namespace Aws
{
namespace IoTFleetWise
{

const auto WAIT_TIME_OUT = std::chrono::seconds( 5 );

/**
 * @brief Wait and retry until the result of func becomes true
 * @param func: The func needs to be retried.
 * @return True when function returns true, or false if a timeout occurs.
 */
bool
waitUntil( std::function<bool()> func )
{
    auto retry_interval_ms = std::chrono::milliseconds( 10 );
    auto startTime = std::chrono::steady_clock::now();
    while ( !func() )
    {
        auto elapsedTime = std::chrono::steady_clock::now() - startTime;
        if ( elapsedTime >= WAIT_TIME_OUT )
        {
            return false;
        }
        std::this_thread::sleep_for( retry_interval_ms );
    }
    return true;
}

/**
 * @brief Repeatedly call the func, then return the last result after the timeout occurs
 * @param func: The func needs to be retried.
 * @return The last result of the func after the timeout.
 */
bool
waitDelay( std::function<bool()> func )
{
    auto retry_interval_ms = std::chrono::milliseconds( 10 );
    auto startTime = std::chrono::steady_clock::now();
    for ( ;; )
    {
        auto res = func();
        auto elapsedTime = std::chrono::steady_clock::now() - startTime;
        if ( elapsedTime >= WAIT_TIME_OUT )
        {
            return res;
        }
        std::this_thread::sleep_for( retry_interval_ms );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
