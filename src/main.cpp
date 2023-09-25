// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ConsoleLogger.h"
#include "IoTFleetWiseConfig.h"
#include "IoTFleetWiseEngine.h"
#include "IoTFleetWiseVersion.h"
#include "LogLevel.h"
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <json/json.h>
#include <string>
#include <unistd.h>

// coverity[autosar_cpp14_a2_11_1_violation]
static volatile sig_atomic_t mSignal = 0; // volatile has to be used since it will be modified by a signal handler,
                                          // executed as result of an asynchronous interrupt

extern "C"
{
    // coverity[cert_msc54_cpp_violation] False positive - function does have C linkage
    static void
    signalHandler( int signum )
    {
        // Very few things are safe in a signal handler. So we never do anything other than set the atomic int, not even
        // print a message: https://stackoverflow.com/a/16891799
        mSignal = signum;
    }
}

static void
printVersion()
{
    std::cout << "Version: " << &VERSION_PROJECT_VERSION[0] << ", git tag: " << &VERSION_GIT_TAG[0]
              << ", git commit sha: " << &VERSION_GIT_COMMIT_SHA[0] << ", Build time: " << &VERSION_BUILD_TIME[0]
              << std::endl;
}

static void
configureLogging( const Json::Value &config )
{
    auto logLevel = Aws::IoTFleetWise::LogLevel::Trace;
    stringToLogLevel( config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString(), logLevel );
    Aws::IoTFleetWise::gSystemWideLogLevel = logLevel;

    auto logColorOption = Aws::IoTFleetWise::LogColorOption::Auto;
    if ( config["staticConfig"]["internalParameters"].isMember( "logColor" ) )
    {
        std::string logColorConfig = config["staticConfig"]["internalParameters"]["logColor"].asString();
        if ( !stringToLogColorOption( logColorConfig, logColorOption ) )
        {
            std::cout << "Invalid logColor config: " << logColorConfig << std::endl;
        }
    }
    Aws::IoTFleetWise::gLogColorOption = logColorOption;
}

static int
signalToExitCode( int signalNumber )
{
    switch ( signalNumber )
    {
    case SIGUSR1:
        std::cout << "Fatal error, stopping" << std::endl;
        return -1;
    case SIGINT:
    case SIGTERM:
        std::cout << "Stopping" << std::endl;
        return 0;
    default:
        std::cout << "Received unexpected signal " << signalNumber << std::endl;
        return 0;
    }
}

int
main( int argc, char *argv[] )
{
    try
    {
        printVersion();
        if ( argc < 2 )
        {
            std::cout << "Error: no config file provided" << std::endl;
            return EXIT_FAILURE;
        }

        signal( SIGINT, signalHandler );
        signal( SIGTERM, signalHandler );
        signal( SIGUSR1, signalHandler );
        std::string configFilename = argv[1];
        Json::Value config;
        if ( !Aws::IoTFleetWise::IoTFleetWiseConfig::read( configFilename, config ) )
        {
            std::cout << "Failed to read config file: " + configFilename << std::endl;
            return EXIT_FAILURE;
        }
        // Set system wide log level
        configureLogging( config );

        Aws::IoTFleetWise::IoTFleetWiseEngine engine;
        // Connect the Engine
        if ( engine.connect( config ) && engine.start() )
        {
            std::cout << "Started successfully" << std::endl;
        }
        else
        {
            return EXIT_FAILURE;
        }

        while ( mSignal == 0 )
        {
            sleep( 1 );
        }
        int exitCode = signalToExitCode( mSignal );
        if ( engine.stop() && engine.disconnect() )
        {
            std::cout << "Stopped successfully" << std::endl;
            return exitCode;
        }

        std::cout << "Stopped with errors" << std::endl;
        return EXIT_FAILURE;
    }
    catch ( const std::exception &e )
    {
        std::cout << "Unhandled exception: " << std::string( e.what() ) << std::endl;
        return EXIT_FAILURE;
    }
    catch ( ... )
    {
        std::cout << "Unknown exception" << std::endl;
        return EXIT_FAILURE;
    }
}
