// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <cstddef>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Keep track of how much memory we are using for AWS SDK
 *
 * Note that this is not an allocator. The methods of this class should be called before calling
 * some SDK operation and after the operation succeeds or fails.
 */
class AwsSDKMemoryManager
{
public:
    static AwsSDKMemoryManager &getInstance();

    /**
     * @brief Set the Limit of maximal memory usage
     *
     * @param size
     * @return true if setting succeeds
     * @return false if setting fails
     */
    bool setLimit( std::size_t size );

    /**
     * @brief Get the Limit object
     *
     * @return std::size_t
     */
    std::size_t getLimit();

    /**
     * @brief Reserve a chunk of memory for usage later
     *
     * @return true if successfully reserved, false if the allocation exceeds the limit
     */
    bool reserveMemory( std::size_t bytes );

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

    static constexpr std::size_t MAXIMUM_AWS_SDK_HEAP_MEMORY_BYTES = 10000000;

    size_t mMaximumAwsSDKMemorySize = MAXIMUM_AWS_SDK_HEAP_MEMORY_BYTES;

    std::mutex mMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
