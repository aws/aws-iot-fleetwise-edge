
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/ConsoleLogger.h"
#include <cstdio>
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{

TEST( LoggingModuleTest, GetErrnoString )
{
    // Try to open a non existing file
    FILE *fp = fopen( "/tmp/fwe_strerror_test/2f1d6e5e5ab1", "rb" );
    (void)fp;
    ASSERT_NE( getErrnoString(), "" );
}

TEST( LoggingModuleTest, stringToLogColorOption )
{
    LogColorOption outLogColorOption;
    EXPECT_TRUE( stringToLogColorOption( "Auto", outLogColorOption ) );
    EXPECT_EQ( outLogColorOption, LogColorOption::Auto );
    EXPECT_TRUE( stringToLogColorOption( "Yes", outLogColorOption ) );
    EXPECT_EQ( outLogColorOption, LogColorOption::Yes );
    EXPECT_TRUE( stringToLogColorOption( "No", outLogColorOption ) );
    EXPECT_EQ( outLogColorOption, LogColorOption::No );
    EXPECT_FALSE( stringToLogColorOption( "Invalid", outLogColorOption ) );
}

} // namespace IoTFleetWise
} // namespace Aws
