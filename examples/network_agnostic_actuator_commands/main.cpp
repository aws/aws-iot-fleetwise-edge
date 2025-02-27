// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AcCommandDispatcher.h"
#include <aws/iotfleetwise/ActuatorCommandManager.h>
#include <aws/iotfleetwise/IoTFleetWiseConfig.h>
#include <aws/iotfleetwise/IoTFleetWiseEngine.h>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

static const std::string AC_COMMAND_INTERFACE_TYPE = "acCommandInterface";

int
main( int argc, char *argv[] )
{
    try
    {
        std::cout << Aws::IoTFleetWise::IoTFleetWiseEngine::getVersion() << std::endl;
        if ( argc < 2 )
        {
            std::cerr << "Error: no config file provided" << std::endl;
            return EXIT_FAILURE;
        }
        std::string configFilename = argv[1];
        auto configFileDirectoryPath = boost::filesystem::absolute( configFilename ).parent_path();

        Aws::IoTFleetWise::IoTFleetWiseEngine::configureSignalHandlers();

        Json::Value config;
        if ( !Aws::IoTFleetWise::IoTFleetWiseConfig::read( configFilename, config ) )
        {
            std::cerr << "Failed to read config file: " + configFilename << std::endl;
            return EXIT_FAILURE;
        }
        Aws::IoTFleetWise::IoTFleetWiseEngine::configureLogging( config );
        Aws::IoTFleetWise::IoTFleetWiseEngine engine;

        /*******************************************************************************************
         * Network agnostic actuator commands example
         *******************************************************************************************/
        std::shared_ptr<AcCommandDispatcher> acCommandDispatcher;
        // Set config hooks to configure network agnostic actuator command:
        engine.setNetworkInterfaceConfigHook(
            [&]( const Aws::IoTFleetWise::IoTFleetWiseConfig &networkInterfaceConfig ) {
                const auto interfaceType = networkInterfaceConfig["type"].asStringRequired();
                const auto interfaceId = networkInterfaceConfig["interfaceId"].asStringRequired();

                // Example actuator interface:
                if ( interfaceType == AC_COMMAND_INTERFACE_TYPE )
                {
                    acCommandDispatcher = std::make_shared<AcCommandDispatcher>();
                    if ( !engine.mActuatorCommandManager->registerDispatcher( interfaceId, acCommandDispatcher ) )
                    {
                        throw std::runtime_error( "Error registering command dispatcher" );
                    }
                    return true;
                }

                return false;
            } );
        /******************************************************************************************/

        if ( ( !engine.connect( config, configFileDirectoryPath ) ) || ( !engine.start() ) )
        {
            return EXIT_FAILURE;
        }
        std::cout << "Started successfully" << std::endl;

        while ( Aws::IoTFleetWise::gSignal == 0 )
        {
            sleep( 1 );
        }
        int exitCode = Aws::IoTFleetWise::IoTFleetWiseEngine::signalToExitCode( Aws::IoTFleetWise::gSignal );
        if ( ( !engine.stop() ) || ( !engine.disconnect() ) )
        {
            std::cerr << "Stopped with errors" << std::endl;
            return EXIT_FAILURE;
        }
        std::cout << "Stopped successfully" << std::endl;
        return exitCode;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Unhandled exception: " << std::string( e.what() ) << std::endl;
        return EXIT_FAILURE;
    }
    catch ( ... )
    {
        std::cerr << "Unknown exception" << std::endl;
        return EXIT_FAILURE;
    }
}
