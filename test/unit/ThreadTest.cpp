// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Thread.h"
#include <gtest/gtest.h>
#include <iostream>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{

void
workerFunction( void *data )
{
    static_cast<void>( data ); // Ignore unused parameter
    std::cout << "Hello from IoTFleetWise";
    usleep( 500000 );
}

TEST( ThreadTest, ThreadCPUUsageInfo )
{
    Thread thread;
    ASSERT_TRUE( !thread.isValid() );
    ASSERT_TRUE( thread.create( workerFunction, this ) );
    ASSERT_TRUE( thread.isActive() );
    ASSERT_TRUE( thread.isValid() );
    thread.release();
    ASSERT_TRUE( !thread.isValid() );
    ASSERT_TRUE( !thread.isActive() );
}

} // namespace IoTFleetWise
} // namespace Aws
