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

#include <atomic>
#include <aws/core/utils/memory/AWSMemory.h>
#include <cstddef>

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{

/**
 * @brief A minimal allocate implementation.
 * In this memory manager, we store the size of the requested memory at the beginning of the allocated block
 * and keep track of how much memory we are allocating and deallocating
 *
 * NOTE: This allocator does not handle over-aligned types . See
 * https://en.cppreference.com/w/cpp/language/object#Alignment.
 * If you are using over-aligned types, do not use this allocator.
 */
class AwsSDKMemoryManager : public Aws::Utils::Memory::MemorySystemInterface
{
public:
    static AwsSDKMemoryManager &getInstance();

    void Begin() override;

    void End() override;

    void *AllocateMemory( std::size_t blockSize, std::size_t alignment, const char *allocationTag = nullptr ) override;

    void FreeMemory( void *memoryPtr ) override;

    /**
     * @brief Reserve a chunk of memory for usage later
     *
     * @return std::size_t Memory size currently in use plus reserved
     */
    std::size_t reserveMemory( std::size_t bytes );

    /**
     * @brief Release the memory reservation.
     * The behavior is undefined if releaseReservedMemory is called without a matching reserveMemory call with the same
     * number of bytes
     *
     * @param bytes The number of bytes to release that were reserved previously
     * @return std::size_t Memory size currently in use plus reserved
     */
    std::size_t releaseReservedMemory( std::size_t bytes );

private:
    AwsSDKMemoryManager() = default;

    /**
     * @brief Usage tracking in terms of how much memory is in use - allocated but not yet deallocated
     *
     */
    std::atomic<std::size_t> mMemoryUsedAndReserved{ 0 };
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws