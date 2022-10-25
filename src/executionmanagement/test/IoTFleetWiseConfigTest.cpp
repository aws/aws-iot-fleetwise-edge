
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
