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

#include <atomic>
#include <aws/crt/Api.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{

extern std::atomic<uint64_t>
    gMemoryUsedBySDK; /**< To prevent the SDK to consume unlimited memory in the scenario of data being
                       * published faster than the internet connection we provide custom allocators to the SDK
                       * and observe the heap memory used by the SDK. So the application can stop sending if the
                       * SDK heap usage exceeds a certain threshold
                       */
using MemBlockSize = uint64_t;

extern void *SdkMemAcquireWrapper( struct aws_allocator *allocator, size_t size );

extern void SdkMemReleaseWrapper( struct aws_allocator *allocator, void *ptr );

extern void *SdkMemReallocWrapper( struct aws_allocator *allocator, void *oldptr, size_t oldsize, size_t newsize );

extern Aws::Crt::Allocator gSDKAllocators;

} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws