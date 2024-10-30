// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsGreengrassV2Receiver.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "LoggingModule.h"
#include "TimeTypes.h"
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <chrono>
#include <future>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

// coverity[autosar_cpp14_a0_1_3_violation] false positive - function overrides sdk's virtual function.
void
SubscribeStreamHandler::OnStreamEvent( Aws::Greengrass::IoTCoreMessage *response )
{
    auto message = response->GetMessage();

    if ( message.has_value() && message.value().GetPayload().has_value() && message.value().GetTopicName().has_value() )
    {
        Timestamp currentTime = mClock->monotonicTimeSinceEpochMs();
        auto payloadBytes = message.value().GetPayload().value();
        auto mqttTopic = std::string( message.value().GetTopicName().value().c_str() );

        mCallback( ReceivedConnectivityMessage{ payloadBytes.data(), payloadBytes.size(), currentTime, mqttTopic } );
    }
}

AwsGreengrassV2Receiver::AwsGreengrassV2Receiver(
    IConnectivityModule *connectivityModule,
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &greengrassClient,
    std::string topicName )
    : mConnectivityModule( connectivityModule )
    , mGreengrassClient( greengrassClient )
    , mSubscribed( false )
    , mTopicName( std::move( topicName ) )
{
}

AwsGreengrassV2Receiver::~AwsGreengrassV2Receiver()
{
    unsubscribe();
}

bool
AwsGreengrassV2Receiver::isAlive()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    return isAliveNotThreadSafe();
}

bool
AwsGreengrassV2Receiver::isAliveNotThreadSafe()
{
    if ( mConnectivityModule == nullptr )
    {
        return false;
    }
    return mConnectivityModule->isAlive() && mSubscribed;
}

ConnectivityError
AwsGreengrassV2Receiver::subscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( mTopicName.empty() )
    {
        FWE_LOG_ERROR( "Empty ingestion topic name provided" );
        return ConnectivityError::NotConfigured;
    }
    if ( !mConnectivityModule->isAlive() )
    {
        FWE_LOG_ERROR( "MQTT Connection not established, failed to subscribe" );
        return ConnectivityError::NoConnection;
    }

    mSubscribeStreamHandler =
        std::make_shared<SubscribeStreamHandler>( [&]( const ReceivedConnectivityMessage &receivedMessage ) {
            mListeners.notify( receivedMessage );
        } );

    if ( mGreengrassClient == nullptr )
    {
        FWE_LOG_ERROR( "mGreengrassClient is null, not initialised" )
        return ConnectivityError::NoConnection;
    }
    mSubscribeOperation = mGreengrassClient->NewSubscribeToIoTCore( mSubscribeStreamHandler );
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

void
AwsGreengrassV2Receiver::subscribeToDataReceived( OnDataReceivedCallback callback )
{
    mListeners.subscribe( callback );
}

bool
AwsGreengrassV2Receiver::unsubscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( isAliveNotThreadSafe() )
    {
        mSubscribeOperation->Close().wait();
        mSubscribed = false;
        return true;
    }
    return false;
}

} // namespace IoTFleetWise
} // namespace Aws
