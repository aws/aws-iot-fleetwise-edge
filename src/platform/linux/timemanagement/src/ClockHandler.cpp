// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "ClockHandler.h"
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{
static std::shared_ptr<const Clock> gClock;
static std::mutex gClockMutex;

class ChronoClock : public Clock
{
public:
    Timestamp
    timeSinceEpochMs() const override
    {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    std::string
    timestampToString() const override
    {
        std::stringstream timeAsString;
        auto timeNow = std::chrono::system_clock::now();
        auto timeNowFormatted = std::chrono::system_clock::to_time_t( timeNow );
        timeAsString << std::put_time( std::localtime( &timeNowFormatted ), "%Y-%m-%d %I:%M:%S %p" );
        return timeAsString.str();
    }
};

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

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
