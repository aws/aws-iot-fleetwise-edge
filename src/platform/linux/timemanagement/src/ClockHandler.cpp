/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

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
static std::shared_ptr<const Clock> gClock;
static std::mutex gClockMutex;

class ChronoClock : public Clock
{
public:
    timestampT
    timeSinceEpochMs() const override
    {
        return static_cast<timestampT>(
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

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
