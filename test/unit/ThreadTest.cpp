// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/Thread.h"
#include <gtest/gtest.h>
#include <iostream>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{

void
workerFunction()
{
    std::cout << "Hello from IoTFleetWise";
    usleep( 500000 );
}

TEST( ThreadTest, ThreadCPUUsageInfo )
{
    Thread thread;
    ASSERT_TRUE( !thread.isValid() );
    ASSERT_TRUE( thread.create( workerFunction ) );
    ASSERT_TRUE( thread.isActive() );
    ASSERT_TRUE( thread.isValid() );
    thread.release();
    ASSERT_TRUE( !thread.isValid() );
    ASSERT_TRUE( !thread.isActive() );
}

} // namespace IoTFleetWise
} // namespace Aws
