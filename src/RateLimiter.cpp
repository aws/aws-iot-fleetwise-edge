// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RateLimiter.h"
#include "Clock.h"

namespace Aws
{
namespace IoTFleetWise
{

RateLimiter::RateLimiter()
    : mMaxTokens( DEFAULT_MAX_TOKENS )
    , mTokenRefillsPerSecond( DEFAULT_TOKEN_REFILLS_PER_SECOND )
    , mCurrentTokens( DEFAULT_MAX_TOKENS )
    , mLastRefillTime( mClock->timeSinceEpoch().monotonicTimeMs )
{
}

RateLimiter::RateLimiter( uint32_t maxTokens, uint32_t tokenRefillsPerSecond )
    : mMaxTokens( maxTokens )
    , mTokenRefillsPerSecond( tokenRefillsPerSecond )
    , mCurrentTokens( maxTokens )
    , mLastRefillTime( mClock->timeSinceEpoch().monotonicTimeMs )
{
}

bool
RateLimiter::consumeToken()
{
    refillTokens();
    if ( mCurrentTokens > 0 )
    {
        --mCurrentTokens;
        return true;
    }
    return false;
}

void
RateLimiter::refillTokens()
{
    auto currTime = mClock->timeSinceEpoch().monotonicTimeMs;
    auto secondsElapsed = ( currTime - mLastRefillTime ) / 1000;
    if ( secondsElapsed > 0 )
    {
        auto newTokens = secondsElapsed * mTokenRefillsPerSecond;
        mCurrentTokens = newTokens >= mMaxTokens ? mMaxTokens : static_cast<uint32_t>( newTokens );
        mLastRefillTime = currTime;
    }
}

} // namespace IoTFleetWise
} // namespace Aws
