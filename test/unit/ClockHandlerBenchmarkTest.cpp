// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Clock.h"
#include "ClockHandler.h"
#include <benchmark/benchmark.h>
#include <memory>

static void
BM_timestampToString( benchmark::State &state )
{
    auto clock = Aws::IoTFleetWise::ClockHandler::getClock();
    for ( auto _ : state )
    {
        clock->currentTimeToIsoString();
    }
}
BENCHMARK( BM_timestampToString );

static void
BM_systemTimeSinceEpochMs( benchmark::State &state )
{
    auto clock = Aws::IoTFleetWise::ClockHandler::getClock();
    for ( auto _ : state )
        clock->systemTimeSinceEpochMs();
}
BENCHMARK( BM_systemTimeSinceEpochMs );

BENCHMARK_MAIN();
