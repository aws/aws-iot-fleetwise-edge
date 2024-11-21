// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RateLimiter.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "TimeTypes.h"
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
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

    uint64_t testDurationSeconds = 3;
    auto startTime = mClock->timeSinceEpoch();
    while ( ( mClock->timeSinceEpoch().monotonicTimeMs - startTime.monotonicTimeMs ) / 1000 < testDurationSeconds )
    {
        if ( rateLimiter->consumeToken() )
        {
            ++consumedTokens;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    ASSERT_TRUE( consumedTokens <= ( numTokens * testDurationSeconds ) );
}

} // namespace IoTFleetWise
} // namespace Aws
