// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

namespace Aws
{
namespace IoTFleetWise
{

class RateLimiterTest : public ::testing::Test
{
protected:
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

TEST_F( RateLimiterTest, ConsumeToken )
{
    uint64_t numTokens = 10;
    auto rateLimiter = std::make_shared<RateLimiter>( numTokens, numTokens );

    uint64_t consumedTokens = 0;

    // Don't wait for full seconds as the rate limiter refills tokens after a whole second passed.
    // So we leave some margin to avoid timing differences that could make the test flaky.
    uint64_t testDurationMs = 2500;
    auto startTime = mClock->timeSinceEpoch();
    FWE_LOG_INFO( "Starting to consume tokens" );
    while ( ( mClock->timeSinceEpoch().monotonicTimeMs - startTime.monotonicTimeMs ) < testDurationMs )
    {
        if ( rateLimiter->consumeToken() )
        {
            ++consumedTokens;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    FWE_LOG_INFO( "Consumed " + std::to_string( consumedTokens ) + " tokens after " + std::to_string( testDurationMs ) +
                  " ms" );
    ASSERT_EQ( consumedTokens,
               numTokens * static_cast<uint64_t>( std::ceil( static_cast<double>( testDurationMs ) / 1000 ) ) );
}

} // namespace IoTFleetWise
} // namespace Aws
