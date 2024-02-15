// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "LoggingModule.h"
#include "Thread.h"
#include "TraceModule.h"
#include <algorithm>
#include <aws/crt/Api.h>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <chrono>
#include <sstream>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

// Default MQTT Keep alive in seconds.
// Defines how often an MQTT PING message is sent to the MQTT broker to keep the connection alive
// Default set to 60 seconds. Every 60 seconds the stack will send an MQTT PING req.
// The longer this interval is, the more the stack takes to detect the state of the TCP connection
// at the lower network layers.
// This parameter is asserted in the C SDK. It shall be strictly bigger than the default
// connection ping timeout( set to 3 seconds).
// Refer to https://github.com/awslabs/aws-c-mqtt/blob/a2ee9a321fcafa19b0473b88a54e0ae8dde5fddf/source/client.c#L1461
constexpr uint16_t MQTT_CONNECT_KEEP_ALIVE_SECONDS = 60;
// Default ping timeout value in milliseconds
// If a response is not received within this interval, the connection will be reestablished.
// If the PING request does not return within this interval, the stack will create a new one.
constexpr uint32_t MQTT_PING_TIMEOUT_MS = 3000;
constexpr uint32_t MQTT_SESSION_EXPIRY_INTERVAL_SEC = 3600;

// How much time to wait for a response to an unsubscribe operation when shutting the module down.
constexpr uint32_t MQTT_UNSUBSCRIBE_TIMEOUT_ON_SHUTDOWN_SEC = 5;

AwsIotConnectivityModule::AwsIotConnectivityModule( std::string rootCA,
                                                    std::string clientId,
                                                    std::shared_ptr<MqttClientBuilderWrapper> mqttClientBuilder )
    : mRootCA( std::move( rootCA ) )
    , mClientId( std::move( clientId ) )
    , mMqttClientBuilder( std::move( mqttClientBuilder ) )
    , mRetryThread( *this, RETRY_FIRST_CONNECTION_START_BACKOFF_MS, RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS )
    , mConnected( false )
    , mConnectionEstablished( false )
{
}

/*
 * As first step to enable backend communication this code is mainly oriented on the basic_pub_sub
 * example from the Aws Iot C++ SDK
 */
bool
AwsIotConnectivityModule::connect()
{
    mConnected = false;

    FWE_LOG_INFO( "Establishing an MQTT Connection" );
    if ( !createMqttConnection() )
    {
        return false;
    }

    return mRetryThread.start();
}

std::shared_ptr<IConnectivityChannel>
AwsIotConnectivityModule::createNewChannel( const std::shared_ptr<PayloadManager> &payloadManager,
                                            const std::string &topicName,
                                            bool subscription )
{
    auto channel = std::make_shared<AwsIotChannel>( this, payloadManager, mMqttClient, topicName, subscription );
    mChannels.emplace_back( channel );
    {
        std::lock_guard<std::mutex> lock( mTopicToChannelMutex );
        mTopicToChannel[topicName] = channel;
    }
    return channel;
}

bool
AwsIotConnectivityModule::resetConnection()
{
    if ( !mConnectionEstablished )
    {
        return false;
    }

    mConnectionCompletedPromise = std::promise<bool>();
    mConnectionClosedPromise = std::promise<void>();
    // Get the future before calling the client code as get_future() is not guaranteed to be thread-safe
    auto closedResult = mConnectionClosedPromise.get_future();
    FWE_LOG_INFO( "Closing the MQTT Connection" );
    if ( !mMqttClient->Stop() )
    {
        FWE_LOG_ERROR( "Failed to close the MQTT Connection" );
        return false;
    }

    closedResult.wait();
    mConnectionEstablished = false;
    return true;
}

bool
AwsIotConnectivityModule::disconnect()
{
    // In case there is no connection or the connection is bad, we don't want to be waiting here for
    // a long time. So we tell all channels to unsubscribe asynchronously, and then wait for them
    // in a separate step.
    std::vector<std::pair<std::string, std::future<bool>>> unsubscribeResults;
    {
        std::lock_guard<std::mutex> lock( mTopicToChannelMutex );
        for ( auto &topicAndChannel : mTopicToChannel )
        {
            unsubscribeResults.emplace_back( topicAndChannel.first, topicAndChannel.second->unsubscribeAsync() );
        }
    }

    auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds( MQTT_UNSUBSCRIBE_TIMEOUT_ON_SHUTDOWN_SEC );
    for ( auto &topicAndResult : unsubscribeResults )
    {
        if ( topicAndResult.second.wait_until( timeout ) == std::future_status::timeout )
        {
            FWE_LOG_WARN( "Unsubscribe operation timed out for topic " + topicAndResult.first );
        }
    }

    mRetryThread.stop();
    for ( auto channel : mChannels )
    {
        channel->invalidateConnection();
    }
    return resetConnection();
}

/**
 * @brief rename current task to kConnectivity to make monitoring easier
 *
 * EventLoopGroup has a  function GetUnderlyingHandle() which member event_loops list points to
 * to struct aws_event_loop which has a member impl_data of type struct epoll_loop *.
 * There the member variable thread_created_on has the pthread id. Sadly struct epoll_loop *
 * seems to be not exposed in any header. So we can not get the pthread_id and need to fallback
 * to set the name on the first callback from the newly created thread
 * */
void
AwsIotConnectivityModule::renameEventLoopTask()
{
    Thread::setCurrentThreadName( "fwCNConnectMod" );
}

bool
AwsIotConnectivityModule::createMqttConnection()
{
    if ( mMqttClientBuilder == nullptr )
    {
        FWE_LOG_ERROR( "Invalid MQTT client builder" );
        return false;
    }

    if ( mClientId.empty() )
    {
        FWE_LOG_ERROR( "Please provide the client ID" );
        return false;
    }

    // Setup connection options
    auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
    // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
    connectOptions->WithClientId( mClientId.c_str() )
        .WithSessionExpiryIntervalSec( MQTT_SESSION_EXPIRY_INTERVAL_SEC )
        .WithKeepAliveIntervalSec( MQTT_CONNECT_KEEP_ALIVE_SECONDS );

    mMqttClientBuilder
        ->WithClientExtendedValidationAndFlowControl(
            Aws::Crt::Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_NONE )
        .WithConnectOptions( connectOptions )
        .WithSessionBehavior( Aws::Crt::Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS )
        .WithPingTimeoutMs( MQTT_PING_TIMEOUT_MS );

    if ( !mRootCA.empty() )
    {
        mMqttClientBuilder->WithCertificateAuthority( Crt::ByteCursorFromCString( mRootCA.c_str() ) );
    }

    mMqttClientBuilder->WithClientConnectionSuccessCallback(
        [&]( const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData ) {
            std::string logMessage = "Connection completed successfully";
            if ( eventData.negotiatedSettings != nullptr )
            {
                // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
                auto clientId = std::string( eventData.negotiatedSettings->getClientId().c_str() );
                logMessage +=
                    ". ClientId: " + clientId + ", SessionExpiryIntervalSec: " +
                    std::to_string( eventData.negotiatedSettings->getSessionExpiryIntervalSec() ) +
                    ", ServerKeepAliveSec: " + std::to_string( eventData.negotiatedSettings->getServerKeepAlive() ) +
                    ", RejoinedSession: " + ( eventData.negotiatedSettings->getRejoinedSession() ? "true" : "false" );
            }
            FWE_LOG_INFO( logMessage );
            mConnected = true;
            mConnectionCompletedPromise.set_value( true );
            mConnectionCompletedPromise = std::promise<bool>();
            renameEventLoopTask();
        } );

    mMqttClientBuilder->WithClientConnectionFailureCallback(
        [&]( const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData ) {
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_FAILED );
            if ( eventData.connAckPacket != nullptr )
            {
                std::string reasonString = std::string( eventData.connAckPacket->getReasonString().has_value()
                                                            ? eventData.connAckPacket->getReasonString()->c_str()
                                                            : "Unknown reason" );
                FWE_LOG_ERROR( "Connection rejected by the server with reason code: " +
                               std::to_string( eventData.connAckPacket->getReasonCode() ) + ": " + reasonString );
            }
            else
            {
                auto errorString = Aws::Crt::ErrorDebugString( eventData.errorCode );
                FWE_LOG_ERROR( "Connection failed with error code " + std::to_string( eventData.errorCode ) + ": " +
                               std::string( errorString != nullptr ? errorString : "Unknown error" ) );
            }
            mConnectionCompletedPromise.set_value( false );
            mConnectionCompletedPromise = std::promise<bool>();
            renameEventLoopTask();
        } );

    mMqttClientBuilder->WithClientAttemptingConnectCallback( [&]( const OnAttemptingConnectEventData &eventData ) {
        (void)eventData;
        FWE_LOG_INFO( "Attempting MQTT connection" );
    } );

    mMqttClientBuilder->WithClientDisconnectionCallback( [&]( const OnDisconnectionEventData &eventData ) {
        // If the disconnect packet is present, it means that the client was disconnected by the server.
        if ( eventData.disconnectPacket != nullptr )
        {
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_INTERRUPTED );
            std::string reasonString = std::string( eventData.disconnectPacket->getReasonString().has_value()
                                                        ? eventData.disconnectPacket->getReasonString()->c_str()
                                                        : "Unknown reason" );
            FWE_LOG_ERROR( "The MQTT connection has been interrupted by the server with reason code: " +
                           std::to_string( eventData.disconnectPacket->getReasonCode() ) + ": " + reasonString );
        }
        else if ( eventData.errorCode == AWS_ERROR_MQTT5_USER_REQUESTED_STOP )
        {
            FWE_LOG_TRACE( "MQTT disconnection requested by the client" );
        }
        else
        {
            auto errorString = Aws::Crt::ErrorDebugString( eventData.errorCode );
            FWE_LOG_ERROR( "Client disconnected with error code " + std::to_string( eventData.errorCode ) + ": " +
                           std::string( errorString != nullptr ? errorString : "Unknown error" ) );
        }
        mConnected = false;
        mConnectionCompletedPromise = std::promise<bool>();
    } );

    mMqttClientBuilder->WithClientStoppedCallback( [&]( const Aws::Crt::Mqtt5::OnStoppedEventData &eventData ) {
        (void)eventData;
        FWE_LOG_INFO( "The MQTT connection is closed and client stopped" );
        mConnectionClosedPromise.set_value();
        mConnected = false;
        mConnectionEstablished = false;
        mConnectionCompletedPromise = std::promise<bool>();
    } );

    mMqttClientBuilder->WithPublishReceivedCallback( [&]( const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData ) {
        std::ostringstream os;
        os << "Data received on the topic: " << eventData.publishPacket->getTopic()
           << " with a payload length of: " << eventData.publishPacket->getPayload().len;
        FWE_LOG_TRACE( os.str() );

        // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
        auto topic = std::string( eventData.publishPacket->getTopic().c_str() );
        std::shared_ptr<AwsIotChannel> channel;
        {
            std::lock_guard<std::mutex> lock( mTopicToChannelMutex );
            auto it = mTopicToChannel.find( topic );
            if ( it != mTopicToChannel.end() )
            {
                channel = it->second;
            }
        }

        if ( channel == nullptr )
        {
            FWE_LOG_ERROR( "Channel not found for topic " + topic );
            return;
        }

        channel->onDataReceived( eventData );
    } );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    mMqttClient = mMqttClientBuilder->Build();
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );
    if ( !mMqttClient )
    {
        int lastError = mMqttClientBuilder->LastError();
        auto errorString = Aws::Crt::ErrorDebugString( lastError );
        FWE_LOG_ERROR( "MQTT Client building failed with error code " + std::to_string( lastError ) + ": " +
                       std::string( errorString != nullptr ? errorString : "Unknown error" ) );
        return false;
    }
    if ( !*mMqttClient )
    {
        int lastError = mMqttClient->LastError();
        auto errorString = Aws::Crt::ErrorDebugString( lastError );
        FWE_LOG_ERROR( "MQTT Client Creation failed with error code " + std::to_string( lastError ) + ": " +
                       std::string( errorString != nullptr ? errorString : "Unknown error" ) );
        return false;
    }

    return true;
}

RetryStatus
AwsIotConnectivityModule::attempt()
{
    FWE_LOG_TRACE( "Starting MQTT client" );

    // Get the future before calling the client code as get_future() is not guaranteed to be thread-safe
    auto connectionResult = mConnectionCompletedPromise.get_future();
    if ( !mMqttClient->Start() )
    {
        int lastError = mMqttClient->LastError();
        auto errorString = Aws::Crt::ErrorDebugString( lastError );
        FWE_LOG_WARN( "The MQTT Connection failed wit error  code " + std::to_string( lastError ) + ": " +
                      std::string( errorString != nullptr ? errorString : "Unknown error" ) );
        mConnectionCompletedPromise = std::promise<bool>();
        return RetryStatus::RETRY;
    }

    FWE_LOG_TRACE( "Waiting of connection completed callback" );
    mConnectionEstablished = true;
    // Block until the connection establishes or fails.
    // If the connection fails, the module will also fail.
    if ( connectionResult.get() )
    {
        return RetryStatus::SUCCESS;
    }
    else
    {
        // Cleanup resources
        resetConnection();
        return RetryStatus::RETRY;
    }
}

void
AwsIotConnectivityModule::onFinished( RetryStatus code )
{
    if ( code == RetryStatus::SUCCESS )
    {
        for ( auto channel : mChannels )
        {
            if ( channel->shouldSubscribeAsynchronously() )
            {
                channel->subscribe();
            }
        }
    }
}

AwsIotConnectivityModule::~AwsIotConnectivityModule()
{
    AwsIotConnectivityModule::disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws
