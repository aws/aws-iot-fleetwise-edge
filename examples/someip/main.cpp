// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MySomeipDataSource.h"
#include "MySomeipInterfaceWrapper.h"
#include "v1/commonapi/MySomeipInterfaceProxy.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <aws/iotfleetwise/CollectionSchemeManager.h>
#include <aws/iotfleetwise/IoTFleetWiseConfig.h>
#include <aws/iotfleetwise/IoTFleetWiseEngine.h>
#include <aws/iotfleetwise/LoggingModule.h>
#include <aws/iotfleetwise/NamedSignalDataSource.h>
#include <boost/filesystem.hpp>
#include <boost/optional/optional.hpp>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>

static const std::string SOMEIP_COLLECTION_INTERFACE_TYPE = "mySomeipCollectionInterface";

static std::shared_ptr<MySomeipInterfaceWrapper>
createMySomeipInterfaceWrapper( const std::string &applicationName, const std::string &myInstance )
{
    return std::make_shared<MySomeipInterfaceWrapper>(
        "local",
        myInstance,
        applicationName,
        []( std::string domain,
            std::string instance,
            std::string connection ) -> std::shared_ptr<v1::commonapi::MySomeipInterfaceProxy<>> {
            return CommonAPI::Runtime::get()->buildProxy<v1::commonapi::MySomeipInterfaceProxy>(
                domain, instance, connection );
        } );
}

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
         * SOME/IP example
         *******************************************************************************************/
        std::unique_ptr<MySomeipDataSource> mySomeipDataSource;
        engine.setNetworkInterfaceConfigHook(
            [&]( const Aws::IoTFleetWise::IoTFleetWiseConfig &networkInterfaceConfig ) {
                const auto interfaceType = networkInterfaceConfig["type"].asStringRequired();
                const auto interfaceId = networkInterfaceConfig["interfaceId"].asStringRequired();

                // Example named signal data source with extra config options and string value ingestion:
                if ( interfaceType == SOMEIP_COLLECTION_INTERFACE_TYPE )
                {
                    // coverity[autosar_cpp14_a20_8_4_violation] Shared pointer interface required for unit testing
                    auto namedSignalDataSource = std::make_shared<Aws::IoTFleetWise::NamedSignalDataSource>(
                        interfaceId, engine.mSignalBufferDistributor );
                    // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                    engine.mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                        std::bind( &Aws::IoTFleetWise::NamedSignalDataSource::onChangeOfActiveDictionary,
                                   namedSignalDataSource.get(),
                                   std::placeholders::_1,
                                   std::placeholders::_2 ) );
                    auto someipCollectionInterfaceConfig = networkInterfaceConfig[SOMEIP_COLLECTION_INTERFACE_TYPE];
                    mySomeipDataSource = std::make_unique<MySomeipDataSource>(
                        createMySomeipInterfaceWrapper(
                            someipCollectionInterfaceConfig["someipApplicationName"].asStringRequired(),
                            someipCollectionInterfaceConfig["someipInstance"].asStringOptional().get_value_or(
                                "commonapi.MySomeipInterface" ) ),
                        std::move( namedSignalDataSource ),
                        someipCollectionInterfaceConfig["cyclicUpdatePeriodMs"].asU32Required() );
                    if ( !mySomeipDataSource->connect() )
                    {
                        FWE_LOG_ERROR( "Failed to initialize SOME/IP data source" );
                        return false;
                    }
                }

                return false;
            } );

        engine.setShutdownConfigHook( [&]() {
            FWE_LOG_INFO( "Stopping the data sources..." );

            mySomeipDataSource.reset();

            return true;
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
