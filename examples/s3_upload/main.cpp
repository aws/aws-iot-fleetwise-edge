// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MyS3Upload.h"
#include <aws/core/VersionConfig.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/iotfleetwise/IoTFleetWiseConfig.h>
#include <aws/iotfleetwise/IoTFleetWiseEngine.h>
#include <aws/iotfleetwise/TransferManagerWrapper.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/S3ServiceClientModel.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/transfer/TransferManager.h>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <string>
#include <unistd.h>

#if ( AWS_SDK_VERSION_MAJOR > 1 ) ||                                                                                   \
    ( ( AWS_SDK_VERSION_MAJOR == 1 ) &&                                                                                \
      ( ( AWS_SDK_VERSION_MINOR > 11 ) || ( ( AWS_SDK_VERSION_MINOR == 11 ) && ( AWS_SDK_VERSION_PATCH >= 224 ) ) ) )
#include <aws/core/utils/threading/PooledThreadExecutor.h>
#else
#include <aws/core/utils/threading/Executor.h>
#endif

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

        if ( ( !engine.connect( config, configFileDirectoryPath ) ) || ( !engine.start() ) )
        {
            return EXIT_FAILURE;
        }
        std::cout << "Started successfully" << std::endl;

        /*******************************************************************************************
         * S3 upload example
         *******************************************************************************************/
        std::unique_ptr<MyS3Upload> myS3Upload;

        auto myS3UploadConfig = config["myS3Upload"];
        auto bucketName = myS3UploadConfig["bucketName"].asString();
        auto bucketRegion = myS3UploadConfig["bucketRegion"].asString();
        auto bucketOwner = myS3UploadConfig["bucketOwner"].asString();
        auto maxConnections = myS3UploadConfig["maxConnections"].asUInt();
        auto localFilePath = myS3UploadConfig["localFilePath"].asString();
        auto remoteObjectKey = myS3UploadConfig["remoteObjectKey"].asString();
        auto transferManagerConfiguration = std::make_shared<Aws::Transfer::TransferManagerConfiguration>( nullptr );

        Aws::S3::Model::PutObjectRequest putObjectTemplate;
        putObjectTemplate.WithExpectedBucketOwner( bucketOwner );
        transferManagerConfiguration->putObjectTemplate = putObjectTemplate;

        transferManagerConfiguration->transferStatusUpdatedCallback =
            // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
            [&]( const Aws::Transfer::TransferManager *transferManager,
                 const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle ) {
                static_cast<void>( transferManager );
                myS3Upload->transferStatusUpdatedCallback( transferHandle );
            };
        transferManagerConfiguration->errorCallback =
            // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
            [&]( const Aws::Transfer::TransferManager *transferManager,
                 const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle,
                 const Aws::Client::AWSError<Aws::S3::S3Errors> &error ) {
                static_cast<void>( transferManager );
                myS3Upload->transferErrorCallback( transferHandle, error );
            };
        transferManagerConfiguration->transferExecutor = engine.getTransferManagerExecutor().get();

        auto createTransferManagerWrapper = [&]() -> std::shared_ptr<Aws::IoTFleetWise::TransferManagerWrapper> {
            Aws::Client::ClientConfigurationInitValues initValues;
            // The SDK can use IMDS to determine the region, but since we will pass the region we don't
            // want the SDK to use it, specially because in non-EC2 environments without any AWS SDK
            // config at all, this can cause delays when setting up the client:
            // https://github.com/aws/aws-sdk-cpp/issues/1511
            initValues.shouldDisableIMDS = true;
            Aws::Client::ClientConfiguration clientConfiguration( initValues );
            clientConfiguration.region = bucketRegion;
            clientConfiguration.maxConnections = maxConnections;
            auto s3Client =
                std::make_shared<Aws::S3::S3Client>( engine.mAwsCredentialsProvider,
                                                     Aws::MakeShared<Aws::S3::S3EndpointProvider>( "S3Client" ),
                                                     clientConfiguration );
            transferManagerConfiguration->s3Client = s3Client;
            return std::make_shared<Aws::IoTFleetWise::TransferManagerWrapper>(
                Aws::Transfer::TransferManager::Create( *transferManagerConfiguration ) );
        };

        myS3Upload = std::make_unique<MyS3Upload>( createTransferManagerWrapper, bucketName );

        // In this demo, just wait 2 seconds for the IoT connection to complete before attempting upload
        // TODO: In production code, you should retry after upload failure
        sleep( 2 );

        myS3Upload->doUpload( localFilePath, remoteObjectKey );
        /******************************************************************************************/

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
