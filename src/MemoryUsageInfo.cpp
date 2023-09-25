// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MemoryUsageInfo.h"
#include <cstdio>
#include <sys/resource.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
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
        mMaxResidentMemorySize = static_cast<size_t>( selfMemoryUsage.ru_maxrss ) * 1024U;
        return true;
    }
}

} // namespace IoTFleetWise
} // namespace Aws
