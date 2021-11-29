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

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "MemoryUsageInfo.h"
#include <atomic>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{

bool
MemoryUsageInfo::reportMemoryUsageInfo()
{
    // Get the memory page size as needed to compute vm and rss later
    std::size_t pageSize = static_cast<std::size_t>( sysconf( _SC_PAGESIZE ) );
    // Read resident and virtual sizes for this process according to
    // https://man7.org/linux/man-pages/man5/proc.5.html
    // Values are not accurate according to the Kernel documentation.
    unsigned long vmSize = 0U;
    unsigned long residentSize = 0U;
    FILE *const memFile = fopen( "/proc/self/statm", "r" );
    if ( memFile == nullptr )
    {
        return false;
    }

    if ( fscanf( memFile, "%lu %lu", &vmSize, &residentSize ) != 2 )
    {
        fclose( memFile );
        return false;
    }
    else
    {
        mResidentMemorySize = residentSize * pageSize;
        mVirtualMemorySize = vmSize * pageSize;
        fclose( memFile );
    }

    // Max resident memory is available through RUSAGE
    struct rusage selfMemoryUsage = {};
    // https://man7.org/linux/man-pages/man2/getrusage.2.html
    // ru_maxrss (since Linux 2.6.32)
    // This is the maximum resident set size used (in kilobytes).
    // For RUSAGE_CHILDREN, this is the resident set size of the
    // largest child, not the maximum resident set size of the
    // process tree.
    if ( getrusage( RUSAGE_SELF, &selfMemoryUsage ) != 0 )
    {
        return false;
    }
    else
    {
        mMaxResidentMemorySize = static_cast<size_t>( selfMemoryUsage.ru_maxrss * 1024U );
        return true;
    }
}

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
