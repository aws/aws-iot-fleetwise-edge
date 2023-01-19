// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes

#include <cstdint>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{

using Timestamp = std::uint64_t;

/**
 * @brief Represents a time point based on different clocks
 *
 * A particular use case for this is when passing down the current time as a parameter.
 * Depending on the situation either system time or a monotonic time should be used, so
 * it is preferable to pass this struct and let the implementation decide which one is
 * needed.
 */
struct TimePoint
{
    Timestamp systemTimeMs;
    Timestamp monotonicTimeMs;
};

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
