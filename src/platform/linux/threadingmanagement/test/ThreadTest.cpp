
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

#include "Thread.h"

#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::Platform;
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
