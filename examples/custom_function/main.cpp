// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionCounter.h"
#include "CustomFunctionFileSize.h"
#include "CustomFunctionSin.h"
#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <aws/iotfleetwise/CollectionInspectionEngine.h>
#include <aws/iotfleetwise/IoTFleetWiseConfig.h>
#include <aws/iotfleetwise/IoTFleetWiseEngine.h>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <string>
#include <unistd.h>

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
         * Custom function examples
         *******************************************************************************************/
        // Set a startup config hook to register custom functions:
        engine.setStartupConfigHook( [&]( const Aws::IoTFleetWise::IoTFleetWiseConfig &config ) {
            // Example 'sin' custom function:
            engine.mCollectionInspectionEngine->registerCustomFunction( "sin",
                                                                        { customFunctionSin, nullptr, nullptr } );

            // Example 'file_size' custom function:
            auto fileSizeFunc = std::make_shared<CustomFunctionFileSize>( engine.mNamedSignalDataSource );
            engine.mCollectionInspectionEngine->registerCustomFunction(
                "file_size",
                { [fileSizeFunc]( auto invocationId, const auto &args ) {
                     return fileSizeFunc->invoke( invocationId, args );
                 },
                  [fileSizeFunc]( const auto &collectedSignalIds, auto timestamp, auto &collectedData ) {
                      fileSizeFunc->conditionEnd( collectedSignalIds, timestamp, collectedData );
                  },
                  nullptr } );

            // Example 'counter' custom function:
            auto counterFunc = std::make_shared<CustomFunctionCounter>();
            engine.mCollectionInspectionEngine->registerCustomFunction(
                "counter",
                { [counterFunc]( auto invocationId, const auto &args ) {
                     return counterFunc->invoke( invocationId, args );
                 },
                  nullptr,
                  [counterFunc]( auto invocationId ) {
                      counterFunc->cleanup( invocationId );
                  } } );
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
