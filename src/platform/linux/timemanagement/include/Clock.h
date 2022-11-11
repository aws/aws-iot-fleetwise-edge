// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "TimeTypes.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{

/**
 * @brief Clock API. Offers the system time since EPOCH.
 */
class Clock
{
public:
    /**
     * @brief Computes the timestamp since epoch from the system clock
     * @return timestamp in milliseconds
     */
    virtual Timestamp timeSinceEpochMs() const = 0;

    /**
     * @brief  Convert the current time to "%Y-%m-%d %I:%M:%S %p" format.
     * @return current time in a string format
     */
    virtual std::string timestampToString() const = 0;

    /**
     * @brief virtual destructor
     */
    virtual ~Clock() = default;
};
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws