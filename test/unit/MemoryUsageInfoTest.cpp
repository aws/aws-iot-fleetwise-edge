
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MemoryUsageInfo.h"

#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{

TEST( MemoryUsageInfoTest, testMemoryUsageInfo )
{
    MemoryUsageInfo usage;

    ASSERT_TRUE( usage.reportMemoryUsageInfo() );
    ASSERT_GE( usage.getMaxResidentMemorySize(), 0 );
    ASSERT_GE( usage.getResidentMemorySize(), 0 );
    ASSERT_GE( usage.getVirtualMemorySize(), 0 );
}

} // namespace IoTFleetWise
} // namespace Aws
