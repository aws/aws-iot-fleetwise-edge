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

#include "IoTSDKCustomAllocators.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{

std::atomic<uint64_t> gMemoryUsedBySDK( 0 );

void *
SdkMemAcquireWrapper( struct aws_allocator *allocator, size_t size )
{
    static_assert( alignof( max_align_t ) >= sizeof( MemBlockSize ), "too big memory size block" );
    (void)allocator;

    MemBlockSize blockSize = size + alignof( max_align_t );
    gMemoryUsedBySDK += blockSize;

    void *blockMem = malloc( blockSize ); // NOLINT(cppcoreguidelines-no-malloc)

    if ( blockMem == nullptr )
    {
        gMemoryUsedBySDK -= blockSize;
        return nullptr;
    }
    memcpy( blockMem, &blockSize, sizeof( blockSize ) );
    return static_cast<void *>( static_cast<char *>( blockMem ) + alignof( max_align_t ) );
}

void
SdkMemReleaseWrapper( struct aws_allocator *allocator, void *ptr )
{
    (void)allocator;
    if ( ptr == nullptr )
    {
        free( ptr ); // NOLINT(cppcoreguidelines-no-malloc)
        return;
    }
    MemBlockSize blockSize = 0;
    void *blockMem = static_cast<void *>( static_cast<char *>( ptr ) - alignof( max_align_t ) );
    memcpy( &blockSize, blockMem, sizeof( blockSize ) );

    free( blockMem ); // NOLINT(cppcoreguidelines-no-malloc)

    gMemoryUsedBySDK -= blockSize;
}

void *
SdkMemReallocWrapper( struct aws_allocator *allocator, void *oldptr, size_t oldsize, size_t newsize )
{
    (void)allocator;
    MemBlockSize blockSize = newsize + alignof( max_align_t );
    if ( newsize > oldsize )
    {
        gMemoryUsedBySDK += ( newsize - oldsize );
    }

    void *oldBlockMem = static_cast<void *>( static_cast<char *>( oldptr ) - alignof( max_align_t ) );
    void *blockMem = realloc( oldBlockMem, blockSize ); // NOLINT(cppcoreguidelines-no-malloc)

    if ( newsize <= oldsize )
    {
        gMemoryUsedBySDK += ( newsize - oldsize );
    }
    if ( blockMem == nullptr )
    {
        gMemoryUsedBySDK -= ( newsize - oldsize );
        return nullptr;
    }
    memcpy( blockMem, &blockSize, sizeof( blockSize ) );
    return static_cast<void *>( static_cast<char *>( blockMem ) + alignof( max_align_t ) );
}

Aws::Crt::Allocator gSDKAllocators{
    SdkMemAcquireWrapper, SdkMemReleaseWrapper, SdkMemReallocWrapper, nullptr, nullptr };
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws