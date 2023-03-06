// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsSDKMemoryManager.h"
#include <aws/core/utils/memory/AWSMemory.h>
#include <cstddef>

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{

namespace
{
using Byte = unsigned char;

// offset to store value of memory size allocated
// Since different types have different alignment requirements,
// we use the largest available alignment offset.
// Note: This however does not work for over-aligned types
// https://en.cppreference.com/w/cpp/language/object#Alignment
// We are OK at the moment to not handled over-aligned types since we do not have any usage of "alignas"
constexpr auto offset = alignof( std::max_align_t );

} // namespace

AwsSDKMemoryManager &
AwsSDKMemoryManager::getInstance()
{
    static AwsSDKMemoryManager instance;
    return instance;
}

void
AwsSDKMemoryManager::Begin()
{
}

void
AwsSDKMemoryManager::End()
{
}

void *
AwsSDKMemoryManager::AllocateMemory( std::size_t blockSize, std::size_t alignment, const char *allocationTag )
{
    // suppress unused parameter errors
    (void)alignment;
    (void)allocationTag;

    // Verify that the object fits into the memory block
    static_assert( offset >= sizeof( std::size_t ), "too big memory size block" );

    auto realSize = blockSize + offset;
    void *pMem = malloc( realSize ); // NOLINT(cppcoreguidelines-no-malloc)

    if ( pMem == nullptr )
    {
        return nullptr;
    }

    // store the allocated memory's size
    *( static_cast<std::size_t *>( pMem ) ) = realSize;
    mMemoryUsedAndReserved += realSize;

    // return a pointer to the block offset from the size storage location
    return static_cast<Byte *>( pMem ) + offset;
}

void
AwsSDKMemoryManager::FreeMemory( void *memoryPtr )
{
    if ( memoryPtr == nullptr )
    {
        return;
    }

    // go back to the memory location where stored the size
    auto pMem = static_cast<void *>( static_cast<Byte *>( memoryPtr ) - offset );
    // read the size value
    auto realSize = *( static_cast<std::size_t *>( pMem ) );

    // free the memory
    free( pMem ); // NOLINT(cppcoreguidelines-no-malloc)

    // update the stats
    mMemoryUsedAndReserved -= realSize;
}

std::size_t
AwsSDKMemoryManager::reserveMemory( std::size_t bytes )
{
    mMemoryUsedAndReserved += ( bytes + offset );
    return mMemoryUsedAndReserved;
}

std::size_t
AwsSDKMemoryManager::releaseReservedMemory( std::size_t bytes )
{
    mMemoryUsedAndReserved -= ( bytes + offset );
    return mMemoryUsedAndReserved;
}

} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws
