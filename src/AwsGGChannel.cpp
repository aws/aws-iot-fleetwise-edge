// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsGGChannel.h"
#include "AwsSDKMemoryManager.h"
#include "CacheAndPersist.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "LoggingModule.h"
#include <chrono>
#include <future>

namespace Aws
{
namespace IoTFleetWise
{

AwsGGChannel::AwsGGChannel( IConnectivityModule *connectivityModule,
                            std::shared_ptr<PayloadManager> payloadManager,
                            std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &ggConnection )
    : mConnectivityModule( connectivityModule )
    , mPayloadManager( std::move( payloadManager ) )
    , mConnection( ggConnection )
    , mSubscribed( false )
    , mSubscribeAsynchronously( false )
{
}

bool
AwsGGChannel::isAlive()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    return isAliveNotThreadSafe();
}

bool
AwsGGChannel::isAliveNotThreadSafe()
{
    if ( mConnectivityModule == nullptr )
    {
        return false;
    }
    return mConnectivityModule->isAlive();
}

void
AwsGGChannel::setTopic( const std::string &topicNameRef, bool subscribeAsynchronously )
{
    if ( topicNameRef.empty() )
    {
        FWE_LOG_ERROR( "Empty ingestion topic name provided" );
    }
    mSubscribeAsynchronously = subscribeAsynchronously;
    mTopicName = topicNameRef;
}

ConnectivityError
AwsGGChannel::subscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( !isTopicValid() )
    {
        FWE_LOG_ERROR( "Empty ingestion topic name provided" );
        return ConnectivityError::NotConfigured;
    }
    if ( !isAliveNotThreadSafe() )
    {
        FWE_LOG_ERROR( "MQTT Connection not established, failed to subscribe" );
        return ConnectivityError::NoConnection;
    }

    mSubscribeStreamHandler = std::make_shared<SubscribeStreamHandler>( [&]( uint8_t *data, size_t size ) {
        notifyListeners<const std::uint8_t *, size_t>( &IReceiverCallback::onDataReceived, data, size );
    } );

    if ( mConnection == nullptr )
    {
        FWE_LOG_ERROR( "mConnection is null, not initialised" )
        return ConnectivityError::NoConnection;
    }
    mSubscribeOperation = mConnection->NewSubscribeToIoTCore( mSubscribeStreamHandler );
    Aws::Greengrass::SubscribeToIoTCoreRequest subscribeRequest;
    subscribeRequest.SetQos( Aws::Greengrass::QOS_AT_LEAST_ONCE );
    subscribeRequest.SetTopicName( mTopicName.c_str() != nullptr ? mTopicName.c_str() : "" );

    FWE_LOG_TRACE( "Attempting to subscribe to " + mTopicName + " topic" );
    auto requestStatus = mSubscribeOperation->Activate( subscribeRequest ).get();
    if ( !requestStatus )
    {
        FWE_LOG_ERROR( "Failed to send subscription request to " + mTopicName + " topic" );
        return ConnectivityError::NoConnection;
    }

    auto subscribeResultFuture = mSubscribeOperation->GetResult();

    // To avoid throwing exceptions, wait on the result for a specified timeout:
    if ( subscribeResultFuture.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::timeout )
    {
        FWE_LOG_ERROR( "Timed out while waiting for response from Greengrass Core" );
        return ConnectivityError::NoConnection;
    }

    auto subscribeResult = subscribeResultFuture.get();
    if ( subscribeResult )
    {
        FWE_LOG_TRACE( "Successfully subscribed to " + mTopicName + " topic" );
        mPayloadCountSent++;
    }
    else
    {
        auto errorType = subscribeResult.GetResultType();
        if ( errorType == OPERATION_ERROR )
        {
            OperationError *error = subscribeResult.GetOperationError();
            /*
             * This pointer can be casted to any error type like so:
             * if(error->GetModelName() == UnauthorizedError::MODEL_NAME)
             *    UnauthorizedError *unauthorizedError = static_cast<UnauthorizedError*>(error);
             */
            if ( error->GetMessage().has_value() )
            {
                auto errString = error->GetMessage().value().c_str();
                FWE_LOG_ERROR( "Greengrass Core responded with an error: " +
                               ( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) ) );
            }
        }
        else
        {
            auto errString = subscribeResult.GetRpcError().StatusToString();
            FWE_LOG_ERROR( "Attempting to receive the response from the server failed with error code: " +
                           std::string( errString.c_str() != nullptr ? errString.c_str() : "Unknown error" ) );
        }
        return ConnectivityError::NoConnection;
    }

    return ConnectivityError::Success;
}

size_t
AwsGGChannel::getMaxSendSize() const
{
    return AWS_IOT_MAX_MESSAGE_SIZE;
}

ConnectivityError
AwsGGChannel::sendBuffer( const std::uint8_t *buf, size_t size, CollectionSchemeParams collectionSchemeParams )
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( !isTopicValid() )
    {
        FWE_LOG_WARN( "Invalid topic provided" );
        return ConnectivityError::NotConfigured;
    }

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_WARN( "No valid data provided" );
        return ConnectivityError::WrongInputData;
    }

    if ( size > getMaxSendSize() )
    {
        FWE_LOG_WARN( "Payload provided is too long" );
        return ConnectivityError::WrongInputData;
    }

    if ( !isAliveNotThreadSafe() )
    {
        FWE_LOG_WARN( "No alive IPC Connection." );
        if ( mPayloadManager != nullptr )
        {
            if ( collectionSchemeParams.persist )
            {
                mPayloadManager->storeData( buf, size, collectionSchemeParams );
            }
            else
            {
                FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
            }
        }
        return ConnectivityError::NoConnection;
    }
    if ( !( AwsSDKMemoryManager::getInstance().reserveMemory( size ) ) )
    {
        FWE_LOG_ERROR( "Not sending out the message  with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory. Payload will be stored" );

        if ( collectionSchemeParams.persist )
        {
            mPayloadManager->storeData( buf, size, collectionSchemeParams );
        }
        else
        {
            FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
        }
        return ConnectivityError::QuotaReached;
    }

    if ( mConnection == nullptr )
    {
        FWE_LOG_ERROR( "mConnection is null, not initialised" )
        return ConnectivityError::NoConnection;
    }
    auto publishOperation = mConnection->NewPublishToIoTCore();
    Aws::Greengrass::PublishToIoTCoreRequest publishRequest;
    publishRequest.SetTopicName( mTopicName.c_str() != nullptr ? mTopicName.c_str() : "" );
    Aws::Crt::Vector<uint8_t> payload( buf, buf + size );
    publishRequest.SetPayload( payload );
    publishRequest.SetQos( Aws::Greengrass::QOS_AT_LEAST_ONCE );

    FWE_LOG_TRACE( "Attempting to publish to " + mTopicName + " topic" );
    auto requestStatus = publishOperation->Activate( publishRequest ).get();
    if ( !requestStatus )
    {
        auto errString = requestStatus.StatusToString();
        FWE_LOG_ERROR( "Failed to publish to " + mTopicName + " topic with error " +
                       std::string( errString.c_str() != nullptr ? errString.c_str() : "Unknown error" ) );

        return ConnectivityError::NoConnection;
    }

    auto publishResultFuture = publishOperation->GetResult();

    // To avoid throwing exceptions, wait on the result for a specified timeout:
    if ( publishResultFuture.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::timeout )
    {
        FWE_LOG_ERROR( "Timed out while waiting for response from Greengrass Core" );
        return ConnectivityError::NoConnection;
    }

    auto publishResult = publishResultFuture.get();
    if ( !publishResult )
    {
        FWE_LOG_ERROR( "Failed to publish to " + mTopicName + " topic" );
        auto errorType = publishResult.GetResultType();
        if ( errorType == OPERATION_ERROR )
        {
            OperationError *error = publishResult.GetOperationError();
            /*
             * This pointer can be casted to any error type like so:
             * if(error->GetModelName() == UnauthorizedError::MODEL_NAME)
             *    UnauthorizedError *unauthorizedError = static_cast<UnauthorizedError*>(error);
             */
            if ( error->GetMessage().has_value() )
            {
                auto errString = error->GetMessage().value().c_str();
                FWE_LOG_ERROR( "Greengrass Core responded with an error: " +
                               ( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) ) );
            }
        }
        else
        {
            auto errString = publishResult.GetRpcError().StatusToString();
            FWE_LOG_ERROR( "Attempting to receive the response from the server failed with error code " +
                           std::string( errString.c_str() != nullptr ? errString.c_str() : "Unknown error" ) );
        }
        return ConnectivityError::NoConnection;
    }

    return ConnectivityError::Success;
}

ConnectivityError
AwsGGChannel::sendFile( const std::string &filePath, size_t size, CollectionSchemeParams collectionSchemeParams )
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( !isTopicValid() )
    {
        FWE_LOG_WARN( "Invalid topic provided" );
        return ConnectivityError::NotConfigured;
    }

    if ( mPayloadManager == nullptr )
    {
        FWE_LOG_WARN( "No payload manager provided" );
        return ConnectivityError::NotConfigured;
    }

    if ( filePath.empty() )
    {
        FWE_LOG_WARN( "No valid file path provided" );
        return ConnectivityError::WrongInputData;
    }

    if ( size > getMaxSendSize() )
    {
        FWE_LOG_WARN( "Payload provided is too long" );
        return ConnectivityError::WrongInputData;
    }

    if ( !isAliveNotThreadSafe() )
    {
        if ( collectionSchemeParams.persist )
        {
            // Only store metadata, file is already written on the disk
            mPayloadManager->storeMetadata( filePath, size, collectionSchemeParams );
        }
        else
        {
            FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
        }
        return ConnectivityError::NoConnection;
    }

    if ( !AwsSDKMemoryManager::getInstance().reserveMemory( size ) )
    {
        FWE_LOG_ERROR( "Not sending out the message  with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory. Currently allocated " );
        {
            if ( collectionSchemeParams.persist )
            {
                // Only store metadata, file is already written on the disk
                mPayloadManager->storeMetadata( filePath, size, collectionSchemeParams );
            }
            else
            {
                FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
            }
        }
        return ConnectivityError::QuotaReached;
    }

    Aws::Crt::Vector<uint8_t> payload( size );
    if ( mPayloadManager->retrievePayload( payload.data(), payload.size(), filePath ) != ErrorCode::SUCCESS )
    {
        return ConnectivityError::WrongInputData;
    }

    if ( mConnection == nullptr )
    {
        FWE_LOG_ERROR( "mConnection is null, not initialised" )
        return ConnectivityError::NoConnection;
    }
    auto publishOperation = mConnection->NewPublishToIoTCore();
    Aws::Greengrass::PublishToIoTCoreRequest publishRequest;
    publishRequest.SetTopicName( mTopicName.c_str() != nullptr ? mTopicName.c_str() : "" );
    publishRequest.SetPayload( payload );
    publishRequest.SetQos( Aws::Greengrass::QOS_AT_LEAST_ONCE );

    FWE_LOG_TRACE( "Attempting to publish to " + mTopicName + " topic" );
    auto requestStatus = publishOperation->Activate( publishRequest ).get();
    if ( !requestStatus )
    {
        FWE_LOG_ERROR( "Failed to publish to " + mTopicName + " topic with error " +
                       std::string( requestStatus.StatusToString().c_str() != nullptr
                                        ? requestStatus.StatusToString().c_str()
                                        : "" ) );
        return ConnectivityError::NoConnection;
    }

    auto publishResultFuture = publishOperation->GetResult();

    // To avoid throwing exceptions, wait on the result for a specified timeout:
    if ( publishResultFuture.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::timeout )
    {
        FWE_LOG_ERROR( "Timed out while waiting for response from Greengrass Core" );
        return ConnectivityError::NoConnection;
    }

    auto publishResult = publishResultFuture.get();
    if ( !publishResult )
    {
        FWE_LOG_ERROR( "Failed to publish to " + mTopicName + " topic" );
        auto errorType = publishResult.GetResultType();
        if ( errorType == OPERATION_ERROR )
        {
            OperationError *error = publishResult.GetOperationError();
            /*
             * This pointer can be casted to any error type like so:
             * if(error->GetModelName() == UnauthorizedError::MODEL_NAME)
             *    UnauthorizedError *unauthorizedError = static_cast<UnauthorizedError*>(error);
             */
            if ( error->GetMessage().has_value() )
            {

                auto errString = error->GetMessage().value().c_str();
                FWE_LOG_ERROR( "Greengrass Core responded with an error: " +
                               ( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) ) );
            }
        }
        else
        {
            auto errString = publishResult.GetRpcError().StatusToString();
            FWE_LOG_ERROR( "Attempting to receive the response from the server failed with error code " +
                           std::string( errString.c_str() != nullptr ? errString.c_str() : "Unknown error" ) );
        }
        return ConnectivityError::NoConnection;
    }

    return ConnectivityError::Success;
}

bool
AwsGGChannel::unsubscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( mSubscribed && isAliveNotThreadSafe() )
    {
        mSubscribeOperation->Close().wait();
        mSubscribed = false;
        return true;
    }
    return false;
}

AwsGGChannel::~AwsGGChannel()
{
    unsubscribe();
}

} // namespace IoTFleetWise
} // namespace Aws
