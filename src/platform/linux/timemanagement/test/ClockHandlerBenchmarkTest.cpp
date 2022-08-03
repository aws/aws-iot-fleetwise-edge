#include <benchmark/benchmark.h>

#include "ClockHandler.h"

using namespace Aws::IoTFleetWise::Platform::Linux;

static void
BM_timestampToString( benchmark::State &state )
{
    auto clock = ClockHandler::getClock();
    for ( auto _ : state )
    {
        clock->timestampToString();
    }
}
BENCHMARK( BM_timestampToString );

static void
BM_timeSinceEpochMs( benchmark::State &state )
{
    auto clock = ClockHandler::getClock();
    for ( auto _ : state )
        clock->timeSinceEpochMs();
}
BENCHMARK( BM_timeSinceEpochMs );

BENCHMARK_MAIN();