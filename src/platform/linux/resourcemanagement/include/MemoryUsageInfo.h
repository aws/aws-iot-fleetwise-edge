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

#pragma once

#if defined( IOTFLEETWISE_LINUX )
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

/**
 * @brief Utility class to track and report Memory Consumption.
 */
class MemoryUsageInfo
{
public:
    /**
     * @brief Updates the current CPU usage stats.
     * @return True if the report succeeded.
     */
    bool reportMemoryUsageInfo();

    /**
     * @brief Gets the maximum resident size.
     *
     * This represents the maximum amount of physical memory used by the process.
     * @return The maximum resident size in bytes.
     */
    std::size_t getMaxResidentMemorySize() const;

    /**
     * @brief Gets the current resident size in bytes.
     *
     * This represents the current amount of physical memory used by the process.
     * @return The current resident size in bytes.
     */
    std::size_t getResidentMemorySize() const;

    /**
     * @brief Gets the current virtual size in bytes.
     *
     * This represents current virtual memory assigned to the process.
     * @return The current virtual size in bytes.
     */
    std::size_t getVirtualMemorySize() const;

private:
    std::size_t mMaxResidentMemorySize{ 0 };
    std::size_t mResidentMemorySize{ 0 };
    std::size_t mVirtualMemorySize{ 0 };
};

inline std::size_t
MemoryUsageInfo::getMaxResidentMemorySize() const
{
    return mMaxResidentMemorySize;
}

inline std::size_t
MemoryUsageInfo::getResidentMemorySize() const
{
    return mResidentMemorySize;
}

inline std::size_t
MemoryUsageInfo::getVirtualMemorySize() const
{
    return mVirtualMemorySize;
}

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
