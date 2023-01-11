// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "TimeTypes.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace TestingSupport
{

using TimePoint = Aws::IoTFleetWise::Platform::Linux::TimePoint;
using Timestamp = Aws::IoTFleetWise::Platform::Linux::Timestamp;

TimePoint
operator+( const TimePoint &time, Timestamp increment )
{
    return { time.systemTimeMs + increment, time.monotonicTimeMs + increment };
}

TimePoint &
operator+=( TimePoint &time, Timestamp increment )
{
    time.systemTimeMs += increment;
    time.monotonicTimeMs += increment;
    return time;
}

TimePoint
operator++( TimePoint &time, int )
{
    time += 1;
    return time;
}

bool
operator==( const TimePoint &left, const TimePoint &right )
{
    return left.systemTimeMs == right.systemTimeMs && left.monotonicTimeMs == right.monotonicTimeMs;
}

} // namespace TestingSupport
} // namespace IoTFleetWise
} // namespace Aws
