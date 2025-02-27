// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/ConsoleLogger.h"
#include "aws/iotfleetwise/LogLevel.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <csignal>
#include <cstdlib>
#include <gtest/gtest.h>

extern "C"
{
    // coverity[cert_msc54_cpp_violation] False positive - function does have C linkage
    static void
    segFaultHandler( int signum )
    {
        static_cast<void>( signum );
        // SIGSEGV handlers should never return. We have to abort:
        // https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
        abort();
    }

    // coverity[cert_msc54_cpp_violation] False positive - function does have C linkage
    static void
    abortHandler( int signum )
    {
        static_cast<void>( signum );
        // Very few things are safe in a signal handler. Flushing streams isn't normally safe,
        // unless we can guarantee that nothing is currently using the stream:
        // https://www.gnu.org/software/libc/manual/html_node/Nonreentrancy.html
        // So we use an atomic int (signal handler safe) to check whether the program stopped while
        // in the middle of a log call. Assuming that we are not using the log stream (stdout)
        // directly anywhere else, flushing should be safe here.
        if ( Aws::IoTFleetWise::gOngoingLogMessage == 0 )
        {
            Aws::IoTFleetWise::LoggingModule::flush();
        }
    }
}

static void
configureLogging()
{
    Aws::IoTFleetWise::gSystemWideLogLevel = Aws::IoTFleetWise::LogLevel::Trace;
    Aws::IoTFleetWise::gLogColorOption = Aws::IoTFleetWise::LogColorOption::Yes;
}

int
main( int argc, char **argv )
{
    signal( SIGSEGV, segFaultHandler );
    // Mainly to handle when a thread is terminated due to uncaught exception
    signal( SIGABRT, abortHandler );
    ::testing::InitGoogleTest( &argc, argv );
    configureLogging();
    return RUN_ALL_TESTS();
}
