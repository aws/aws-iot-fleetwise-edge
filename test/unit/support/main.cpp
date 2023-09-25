// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ConsoleLogger.h"
#include "LogLevel.h"
#include <gtest/gtest.h>

static void
configureLogging()
{
    Aws::IoTFleetWise::gSystemWideLogLevel = Aws::IoTFleetWise::LogLevel::Trace;
    Aws::IoTFleetWise::gLogColorOption = Aws::IoTFleetWise::LogColorOption::Yes;
}

int
main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    configureLogging();
    return RUN_ALL_TESTS();
}
