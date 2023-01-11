#include "ConsoleLogger.h"
#include "LogLevel.h"
#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::Platform::Linux;

static void
configureLogging()
{
    gSystemWideLogLevel = LogLevel::Trace;
    gLogColorOption = LogColorOption::Yes;
}

int
main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    configureLogging();
    return RUN_ALL_TESTS();
}