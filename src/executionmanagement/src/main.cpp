/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

// Includes
#include "IoTFleetWiseConfig.h"
#include "IoTFleetWiseEngine.h"
#include "IoTFleetWiseVersion.h"
#include "LogLevel.h"
#include <csignal>
#include <cstdlib>
#include <iostream>

using namespace Aws::IoTFleetWise::ExecutionManagement;

bool mSignal = false;

static void
signalHandler( int signum )
{
    static_cast<void>( signum ); // unused parameter
    std::cout << "Stopping AWS IoT FleetWise Edge Service " << std::endl;

    mSignal = true;
}

static void
printVersion()
{
    std::cout << "Version: " << VERSION_PROJECT_VERSION << ", git tag: " << VERSION_GIT_TAG
              << ", git commit sha: " << VERSION_GIT_COMMIT_SHA << ", Build time: " << VERSION_BUILD_TIME << std::endl;
}

static void
setSystemWideLogLevel( const Json::Value &config )
{
    Aws::IoTFleetWise::Platform::Linux::LogLevel logLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Trace;
    stringToLogLevel( config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString(), logLevel );
    gSystemWideLogLevel = logLevel;
}

int
main( int argc, char *argv[] )
{
    printVersion();
    if ( argc != 2 )
    {
        std::cout << "error: invalid argument - only a config file is required" << std::endl;
        return EXIT_FAILURE;
    }

    IoTFleetWiseEngine engine;
    signal( SIGINT, signalHandler );
    std::string configFilename = argv[1];
    Json::Value config;
    if ( !IoTFleetWiseConfig::read( configFilename, config ) )
    {
        std::cout << " AWS IoT FleetWise Edge Service failed to read config file: " + configFilename << std::endl;
        return EXIT_FAILURE;
    }
    // Set system wide log level
    setSystemWideLogLevel( config );

    // Connect the Engine
    if ( engine.connect( config ) && engine.start() )
    {
        std::cout << " AWS IoT FleetWise Edge Service Started successfully " << std::endl;
    }
    else
    {
        return EXIT_FAILURE;
    }

    while ( !mSignal )
    {
        sleep( 1 );
    }
    if ( engine.stop() && engine.disconnect() )
    {
        std::cout << " AWS IoT FleetWise Edge Service Stopped successfully " << std::endl;
        return EXIT_SUCCESS;
    }

    std::cout << " AWS IoT FleetWise Edge Service Stopped with errors " << std::endl;
    return EXIT_FAILURE;
}
