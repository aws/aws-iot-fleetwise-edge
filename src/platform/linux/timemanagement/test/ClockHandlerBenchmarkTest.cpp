#include <benchmark/benchmark.h>

#include "ClockHandler.h"

using namespace Aws::IoTFleetWise::Platform::Linux;

static void
BM_timestampToString( benchmark::State &state )
{
    auto clock = ClockHandler::getClock();
    for ( auto _ : state )
    {
        clock->currentTimeToIsoString();
    }
}
BENCHMARK( BM_timestampToString );

static void
BM_systemTimeSinceEpochMs( benchmark::State &state )
{
    auto clock = ClockHandler::getClock();
    for ( auto _ : state )
        clock->systemTimeSinceEpochMs();
}
BENCHMARK( BM_systemTimeSinceEpochMs );

BENCHMARK_MAIN();
