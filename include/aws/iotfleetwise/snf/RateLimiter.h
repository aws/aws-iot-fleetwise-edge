// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <cstdint>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

static constexpr std::uint32_t DEFAULT_MAX_TOKENS = 100;
static constexpr std::uint32_t DEFAULT_TOKEN_REFILLS_PER_SECOND = DEFAULT_MAX_TOKENS;

class RateLimiter
{
public:
    RateLimiter();
    RateLimiter( uint32_t maxTokens, uint32_t tokenRefillsPerSecond );
    bool consumeToken();

private:
    void refillTokens();

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    uint32_t mMaxTokens;
    uint32_t mTokenRefillsPerSecond;
    uint32_t mCurrentTokens;
    Timestamp mLastRefillTime;
};
} // namespace IoTFleetWise
} // namespace Aws
