// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsGreengrassV2Sender.h"
#include "AwsSDKMemoryManager.h"
#include "IConnectionTypes.h"
#include "IConnectivityModule.h"
#include "LoggingModule.h"
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <chrono>
#include <functional>
#include <future>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

AwsGreengrassV2Sender::AwsGreengrassV2Sender(
    IConnectivityModule *connectivityModule,
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &greengrassClient,
    std::string topicName,
    Aws::Greengrass::QOS publishQoS )
    : mConnectivityModule( connectivityModule )
    , mGreengrassClient( greengrassClient )
    , mPublishQoS( publishQoS )
    , mTopicName( std::move( topicName ) )
{
}

bool
AwsGreengrassV2Sender::isAlive()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    return isAliveNotThreadSafe();
}

bool
AwsGreengrassV2Sender::isAliveNotThreadSafe()
{
    if ( mConnectivityModule == nullptr )
    {
        return false;
    }
    return mConnectivityModule->isAlive();
}

size_t
AwsGreengrassV2Sender::getMaxSendSize() const
{
    return AWS_IOT_MAX_MESSAGE_SIZE;
}

void
AwsGreengrassV2Sender::sendBuffer( const std::uint8_t *buf, size_t size, OnDataSentCallback callback )
{
    sendBufferToTopic( mTopicName, buf, size, callback );
}

void
AwsGreengrassV2Sender::sendBufferToTopic( const std::string &topic,
                                          const uint8_t *buf,
                                          size_t size,
                                          OnDataSentCallback callback )
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( topic.empty() )
    {
        FWE_LOG_WARN( "Invalid topic provided" );
        callback( ConnectivityError::NotConfigured );
        return;
    }

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_WARN( "No valid data provided" );
        callback( ConnectivityError::WrongInputData );
        return;
    }

    if ( size > getMaxSendSize() )
    {
        FWE_LOG_WARN( "Payload provided is too long" );
        callback( ConnectivityError::WrongInputData );
        return;
    }

    if ( !isAliveNotThreadSafe() )
    {
        FWE_LOG_WARN( "No alive IPC Connection." );
        callback( ConnectivityError::NoConnection );
        return;
    }

    if ( !AwsSDKMemoryManager::getInstance().reserveMemory( size ) )
    {
        FWE_LOG_ERROR( "Not sending out the message with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory." );

        callback( ConnectivityError::QuotaReached );
        return;
    }

    if ( mGreengrassClient == nullptr )
    {
        FWE_LOG_ERROR( "mGreengrassClient is null, not initialised" )
        callback( ConnectivityError::NoConnection );
        return;
    }
    auto publishOperation = mGreengrassClient->NewPublishToIoTCore();
    Aws::Greengrass::PublishToIoTCoreRequest publishRequest;
    publishRequest.SetTopicName( topic.c_str() != nullptr ? topic.c_str() : "" );
    Aws::Crt::Vector<uint8_t> payload( buf, buf + size );
    publishRequest.SetPayload( payload );
    publishRequest.SetQos( mPublishQoS );

    FWE_LOG_TRACE( "Attempting to publish to " + topic + " topic" );
    auto onMessageFlushCallback = [callback, topicName = topic]( int errorCode ) {
        if ( errorCode != 0 )
        {
            FWE_LOG_ERROR( "Failed to publish to " + topicName + " topic with error code " +
                           std::to_string( errorCode ) );
            callback( ConnectivityError::TransmissionError );
            return;
        }
        callback( ConnectivityError::Success );
    };
    auto requestStatus = publishOperation->Activate( publishRequest, onMessageFlushCallback ).get();
    if ( !requestStatus )
    {
        auto errString = requestStatus.StatusToString();
        FWE_LOG_ERROR( "Failed to publish to " + topic + " topic with error " +
                       std::string( errString.c_str() != nullptr ? errString.c_str() : "Unknown error" ) );

        callback( ConnectivityError::NoConnection );
        return;
    }

    auto publishResultFuture = publishOperation->GetResult();

    // To avoid throwing exceptions, wait on the result for a specified timeout:
    if ( publishResultFuture.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::timeout )
    {
        FWE_LOG_ERROR( "Timed out while waiting for response from Greengrass Core" );
        callback( ConnectivityError::NoConnection );
        return;
    }

    auto publishResult = publishResultFuture.get();
    if ( !publishResult )
    {
        FWE_LOG_ERROR( "Failed to publish to " + topic + " topic" );
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
        callback( ConnectivityError::NoConnection );
        return;
    }

    callback( ConnectivityError::Success );
}

} // namespace IoTFleetWise
} // namespace Aws
