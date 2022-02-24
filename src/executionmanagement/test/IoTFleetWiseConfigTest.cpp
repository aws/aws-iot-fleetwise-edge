
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

#include "IoTFleetWiseConfig.h"
#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::ExecutionManagement;

TEST( IoTFleetWiseConfigTest, BadFileName )
{
    Json::Value config;
    ASSERT_FALSE( IoTFleetWiseConfig::read( "bad-file-name.json", config ) );
}

TEST( IoTFleetWiseConfigTest, ReadOk )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "em-example-config.json", config ) );
    ASSERT_EQ( 10000, config["staticConfig"]["bufferSizes"]["socketCANBufferSize"].asInt() );
    ASSERT_EQ( 10000, config["staticConfig"]["bufferSizes"]["decodedSignalsBufferSize"].asInt() );
    ASSERT_EQ( 10000, config["staticConfig"]["bufferSizes"]["rawCANFrameBufferSize"].asInt() );
    ASSERT_EQ( "Trace", config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString() );
}
