// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <aws/iotfleetwise/IConnectionTypes.h>
#include <aws/iotfleetwise/IConnectivityModule.h>
#include <aws/iotfleetwise/IReceiver.h>
#include <aws/iotfleetwise/ISender.h>
#include <aws/iotfleetwise/IoTFleetWiseConfig.h>
#include <aws/iotfleetwise/IoTFleetWiseEngine.h>
#include <aws/iotfleetwise/LoggingModule.h>
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
         * MQTT pub/sub example
         *******************************************************************************************/
        // Set a startup config hook to publish and subscribe to an MQTT topic:
        std::shared_ptr<Aws::IoTFleetWise::ISender> sender;
        std::shared_ptr<Aws::IoTFleetWise::IReceiver> receiver;
        engine.setStartupConfigHook( [&]( const Aws::IoTFleetWise::IoTFleetWiseConfig &config ) {
            sender = engine.mConnectivityModule->createSender();
            receiver = engine.mConnectivityModule->createReceiver( "ping" );
            receiver->subscribeToDataReceived(
                [&]( const Aws::IoTFleetWise::ReceivedConnectivityMessage &receivedMessage ) {
                    std::string payload( receivedMessage.buf, receivedMessage.buf + receivedMessage.size );
                    FWE_LOG_INFO( "Received message on topic " + receivedMessage.mqttTopic + " with payload " +
                                  payload + ". Sending pong..." );
                    sender->sendBuffer( "pong",
                                        receivedMessage.buf,
                                        receivedMessage.size,
                                        [&]( Aws::IoTFleetWise::ConnectivityError result ) {
                                            if ( result != Aws::IoTFleetWise::ConnectivityError::Success )
                                            {
                                                FWE_LOG_ERROR( "Error sending pong: " +
                                                               Aws::IoTFleetWise::connectivityErrorToString( result ) );
                                                return;
                                            }
                                            FWE_LOG_INFO( "Pong sent successfully" );
                                        } );
                } );
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
