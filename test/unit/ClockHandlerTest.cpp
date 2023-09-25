// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ClockHandler.h"
#include "Clock.h"
#include <gtest/gtest.h>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

TEST( ClockHandlerTest, systemTimeSinceEpochMs )
{
    auto clock = ClockHandler::getClock();
    ASSERT_NE( clock.get(), nullptr );
    ASSERT_GT( clock->systemTimeSinceEpochMs(), 0ull );
}

} // namespace IoTFleetWise
} // namespace Aws
