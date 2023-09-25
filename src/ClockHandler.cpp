// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ClockHandler.h"
#include "TimeTypes.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

static std::shared_ptr<const Clock> gClock;
static std::mutex gClockMutex;

class ChronoClock : public Clock
{
public:
    Timestamp
    systemTimeSinceEpochMs() const override
    {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    std::string
    currentTimeToIsoString() const override
    {
        std::stringstream timeAsString;
        auto timeNow = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( timeNow.time_since_epoch() ).count() % 1000;
        auto timeNowFormatted = std::chrono::system_clock::to_time_t( timeNow );
        struct tm tmStruct = {};
        // coverity[misra_cpp_2008_rule_18_0_4_violation] No C++14 method exists to convert from time_t to struct tm
        timeAsString << std::put_time( gmtime_r( &timeNowFormatted, &tmStruct ), "%FT%T." ) << std::setfill( '0' )
                     << std::setw( 3 ) << ms << "Z";
        return timeAsString.str();
    }

    Timestamp
    monotonicTimeSinceEpochMs() const override
    {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now().time_since_epoch() )
                .count() );
    }

    TimePoint
    timeSinceEpoch() const override
    {
        return TimePoint{ systemTimeSinceEpochMs(), monotonicTimeSinceEpochMs() };
    }
};

TimePoint
timePointFromSystemTime( const TimePoint &currTime, Timestamp systemTimeMs )
{
    if ( systemTimeMs >= currTime.systemTimeMs )
    {
        Timestamp differenceMs = systemTimeMs - currTime.systemTimeMs;
        return TimePoint{ systemTimeMs, currTime.monotonicTimeMs + differenceMs };
    }
    else
    {
        Timestamp differenceMs = currTime.systemTimeMs - systemTimeMs;
        if ( differenceMs <= currTime.monotonicTimeMs )
        {
            return TimePoint{ systemTimeMs, currTime.monotonicTimeMs - differenceMs };
        }
        else
        {
            // Not much we can do here. The system time corresponds to a time in the past before the monotonic
            // clock started ticking, so we would need to represent it as a negative number. This could happen in the
            // case the obtained timestamp is completely out of sync with the system time, or when the system time
            // changes between the moment the timestamp was extracted and the moment we are checking here.
            //
            // For example:
            // 1. Timestamp is extracted from the message, corresponding to 08:00:00
            // 2. System time is changed to 2 hours into the future
            // 3. We obtain the current system time (which is now 10:00:00) and monotonic time
            // 4. If the monotonic time is small enough (e.g. less than 2 * 60 * 60 * 1000 = 7200000 ms), this situation
            // will happen.
            return TimePoint{ 0, 0 };
        }
    }
}

std::shared_ptr<const Clock>
ClockHandler::getClock()
{
    std::lock_guard<std::mutex> lock( gClockMutex );
    if ( !gClock )
    {
        static std::shared_ptr<const ChronoClock> defaultClock = std::make_shared<ChronoClock>();
        gClock = defaultClock;
    }
    return gClock;
}

void
ClockHandler::setClock( std::shared_ptr<const Clock> clock )
{
    std::lock_guard<std::mutex> lock( gClockMutex );
    gClock = std::move( clock );
}

} // namespace IoTFleetWise
} // namespace Aws
