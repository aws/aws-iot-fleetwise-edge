// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MyCounterDataSource.h"
#include "MyCustomDataSource.h"
#include <atomic>
#include <aws/iotfleetwise/CollectionSchemeManager.h>
#include <aws/iotfleetwise/IoTFleetWiseConfig.h>
#include <aws/iotfleetwise/IoTFleetWiseEngine.h>
#include <aws/iotfleetwise/LoggingModule.h>
#include <aws/iotfleetwise/NamedSignalDataSource.h>
#include <aws/iotfleetwise/SignalTypes.h>
#include <aws/iotfleetwise/Thread.h>
#include <boost/filesystem.hpp>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

static const std::string MY_COUNTER_INTERFACE_TYPE = "myCounterInterface";
static const std::string MY_CUSTOM_INTERFACE_TYPE = "myCustomInterface";

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
         * Network agnostic data collection example
         *******************************************************************************************/
        // Set config hooks to configure network agnostic data sources:
        std::thread locationThread;
        std::atomic_bool locationThreadShouldStop( false );
        engine.setStartupConfigHook( [&]( const Aws::IoTFleetWise::IoTFleetWiseConfig &config ) {
            // Example data source that uses the built-in NamedSignalDataSource:
            if ( engine.mNamedSignalDataSource == nullptr )
            {
                FWE_LOG_INFO( "No namedSignalInterface configured. Location data not available" );
            }
            else
            {
                locationThread = std::thread( [&]() {
                    Aws::IoTFleetWise::Thread::setCurrentThreadName( "Location" );

                    // Generates random Lat/Long signal values
                    std::random_device rd;
                    std::mt19937 longGen( rd() );
                    std::mt19937 latGen( rd() );
                    std::uniform_real_distribution<> longDist( -180.0, 180.0 );
                    std::uniform_real_distribution<> latDist( -90.0, 90.0 );
                    while ( !locationThreadShouldStop )
                    {
                        std::vector<std::pair<std::string, Aws::IoTFleetWise::DecodedSignalValue>> values;
                        auto longitude = longDist( longGen );
                        auto latitude = latDist( latGen );
                        values.emplace_back(
                            "Vehicle.CurrentLocation.Longitude",
                            Aws::IoTFleetWise::DecodedSignalValue( longitude, Aws::IoTFleetWise::SignalType::DOUBLE ) );
                        values.emplace_back(
                            "Vehicle.CurrentLocation.Latitude",
                            Aws::IoTFleetWise::DecodedSignalValue( latitude, Aws::IoTFleetWise::SignalType::DOUBLE ) );
                        // This is the API used to inject the data into the Signal Buffers
                        // Passing zero as the timestamp will use the current system time
                        engine.mNamedSignalDataSource->ingestMultipleSignalValues( 0, values );
                        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                    }
                } );
            }
        } );

        std::shared_ptr<MyCounterDataSource> myCounterDataSource;
        std::shared_ptr<MyCustomDataSource> myCustomDataSource;
        engine.setNetworkInterfaceConfigHook(
            [&]( const Aws::IoTFleetWise::IoTFleetWiseConfig &networkInterfaceConfig ) {
                const auto interfaceType = networkInterfaceConfig["type"].asStringRequired();
                const auto interfaceId = networkInterfaceConfig["interfaceId"].asStringRequired();

                // Example named signal data source with extra config options and string value ingestion:
                if ( interfaceType == MY_COUNTER_INTERFACE_TYPE )
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
                    auto myCounterInterfaceConfig = networkInterfaceConfig[MY_COUNTER_INTERFACE_TYPE];
                    myCounterDataSource = std::make_shared<MyCounterDataSource>(
                        namedSignalDataSource,
                        myCounterInterfaceConfig[MyCounterDataSource::CONFIG_OPTION_1].asU32Required(),
                        engine.mRawDataBufferManager.get() );
                    return true;
                }

                // Example custom data source using CSV decoder strings:
                if ( interfaceType == MY_CUSTOM_INTERFACE_TYPE )
                {
                    myCustomDataSource =
                        std::make_shared<MyCustomDataSource>( interfaceId, engine.mSignalBufferDistributor );
                    // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                    engine.mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                        std::bind( &MyCustomDataSource::onChangeOfActiveDictionary,
                                   myCustomDataSource.get(),
                                   std::placeholders::_1,
                                   std::placeholders::_2 ) );
                    return true;
                }

                return false;
            } );

        engine.setShutdownConfigHook( [&]() {
            FWE_LOG_INFO( "Stopping the data sources..." );

            locationThreadShouldStop = true;
            locationThread.join();

            myCounterDataSource.reset();

            myCustomDataSource.reset();

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
