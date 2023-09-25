// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "TimeTypes.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Clock API. Offers the time based on multiple clocks.
 */
class Clock
{
public:
    /**
     * @brief Computes the timestamp since Unix epoch from the system clock
     *
     * This should not be used for measuring intervals, elapsed time, duration, etc. Use
     * monotonicTimeSinceEpochMs() instead.
     *
     * @return timestamp in milliseconds
     */
    virtual Timestamp systemTimeSinceEpochMs() const = 0;

    /**
     * @brief Computes the timestamp since epoch from a monotonic clock
     *
     * Note that epoch in this case is not the Unix timestamp epoch, but rather it can be
     * any arbitrary moment (e.g. since the system started)
     *
     * @return timestamp in milliseconds
     */
    virtual Timestamp monotonicTimeSinceEpochMs() const = 0;

    /**
     * @brief Computes the timestamp since epoch from multiple clocks
     *
     * Note that epoch in this case is not necessarily the Unix timestamp epoch. For a monotonic clock,
     * for example, it can be any arbitrary moment (e.g. since the system started).
     *
     * @return a TimePoint struct containing the time based on different clocks.
     *  WARNING: Since there is no way to atomically get the clock from multiple sources, those times could be
     *  slightly out of sync, especially if the thread is interrupted between different clock calls.
     */
    virtual TimePoint timeSinceEpoch() const = 0;

    /**
     * @brief  Convert the current time to ISO 8601 format.
     * @return current time in a string format
     */
    virtual std::string currentTimeToIsoString() const = 0;

    /**
     * @brief virtual destructor
     */
    virtual ~Clock() = default;
};

/**
 * @brief Computes the monotonic timestamp corresponding to the given system timestamp
 *
 * This is intended to be used when the timestamp is extracted from the data as system time only.
 * But when calculating intervals we need to use a monotonic clock to be resilient to system time changes.
 * So this function can be used to calculate a monotonic time based on the current time.
 *
 * Please note that this is not always possible. In situations where the calculated monotonic time would
 * be negative, TimePoint {0, 0} is returned.
 *
 * @return a TimePoint struct containing the given system time and the corresponding monotonic time
 */
TimePoint timePointFromSystemTime( const TimePoint &currTime, Timestamp systemTimeMs );

} // namespace IoTFleetWise
} // namespace Aws
