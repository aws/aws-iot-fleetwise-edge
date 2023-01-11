
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ClockHandler.h"

#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::Platform::Linux;

TEST( ClockHandlerTest, systemTimeSinceEpochMs )
{
    auto clock = ClockHandler::getClock();
    ASSERT_NE( clock.get(), nullptr );
    ASSERT_GT( clock->systemTimeSinceEpochMs(), 0ull );
}
