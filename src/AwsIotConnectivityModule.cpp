// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/AwsIotConnectivityModule.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <aws/crt/Api.h>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

AwsIotConnectivityModule::AwsIotConnectivityModule( std::string rootCA,
                                                    std::string clientId,
                                                    MqttClientBuilderWrapper &mqttClientBuilder,
                                                    const TopicConfig &topicConfig,
                                                    AwsIotConnectivityConfig connectionConfig )
    : mRootCA( std::move( rootCA ) )
    , mClientId( std::move( clientId ) )
    , mMqttClientBuilder( mqttClientBuilder )
    , mTopicConfig( topicConfig )
    , mConnectionConfig( std::move( connectionConfig ) )
    , mInitialConnectionThread(
          [this]() -> RetryStatus {
              return this->connectMqttClient();
          },
          RETRY_FIRST_CONNECTION_START_BACKOFF_MS,
          RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS )
    , mSubscriptionsThread(
          [this]() -> RetryStatus {
              return this->subscribeAllReceivers();
          },
          RETRY_FIRST_CONNECTION_START_BACKOFF_MS,
          RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS )
    , mConnected( false )
    , mConnectionEstablished( false )
{
    if ( !createMqttClient() )
    {
        throw std::runtime_error( "Failed to create MQTT client" );
    }
}

/*
 * As first step to enable backend communication this code is mainly oriented on the basic_pub_sub
 * example from the Aws Iot C++ SDK
 */
bool
AwsIotConnectivityModule::connect()
{
    mConnected = false;
    return mInitialConnectionThread.start();
}

std::shared_ptr<ISender>
AwsIotConnectivityModule::createSender()
{
    auto sender = std::make_shared<AwsIotSender>( this, *mMqttClient, mTopicConfig );
    mSenders.emplace_back( sender );
    return sender;
}

std::shared_ptr<IReceiver>
AwsIotConnectivityModule::createReceiver( const std::string &topicName )
{
    auto receiver = std::make_shared<AwsIotReceiver>( *mMqttClient, topicName );
    mReceivers.emplace_back( receiver );

    std::lock_guard<std::mutex> lock( mTopicsMutex );
    mSubscribedTopicToReceiver[topicName] = receiver;
    mSubscribedTopicsTree.insert( topicName, receiver );
    return receiver;
}

void
AwsIotConnectivityModule::subscribeToConnectionEstablished( OnConnectionEstablishedCallback callback )
{
    mConnectionEstablishedListeners.subscribe( callback );
}

bool
AwsIotConnectivityModule::resetConnection()
{
    if ( !mConnectionEstablished )
    {
        return true;
    }

    mConnectionCompletedPromise = std::promise<bool>();
    mConnectionClosedPromise = std::promise<void>();
    // Get the future before calling the client code as get_future() is not guaranteed to be thread-safe
    auto closedResult = mConnectionClosedPromise.get_future();
    FWE_LOG_INFO( "Closing the MQTT Connection" );
    // Before we close the connection, we need to create a Disconnect Packet
    // So that we indicate to the broker that we intend to close the session,
    // Otherwise, the socket will simply be closed and thus the broker thinks
    // the connection is lost.
    // Default constructor of DisconnectPacket sets the disconnectReason to
    // CLIENT_INITIATED_DISCONNECT which is what we want here.
    auto disconnectPacket = std::make_shared<Aws::Crt::Mqtt5::DisconnectPacket>();

    if ( !mMqttClient->Stop( disconnectPacket ) )
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
    mSubscriptionsThread.stop();

    // Only unsubscribe from topics if not using persistent sessions. Otherwise when reconnecting
    // the broker won't send messages sent while the client was disconnected.
    if ( mConnectionConfig.sessionExpiryIntervalSeconds == 0 )
    {
        // In case there is no connection or the connection is bad, we don't want to be waiting here for
        // a long time. So we tell all receivers to unsubscribe asynchronously, and then wait for them
        // in a separate step.
        std::vector<std::pair<std::string, std::future<bool>>> unsubscribeResults;
        {
            std::lock_guard<std::mutex> lock( mTopicsMutex );
            for ( auto &topicAndReceiver : mSubscribedTopicToReceiver )
            {
                unsubscribeResults.emplace_back( topicAndReceiver.first, topicAndReceiver.second->unsubscribeAsync() );
            }
        }

        auto timeout =
            std::chrono::steady_clock::now() + std::chrono::seconds( MQTT_UNSUBSCRIBE_TIMEOUT_ON_SHUTDOWN_SECONDS );
        for ( auto &topicAndResult : unsubscribeResults )
        {
            if ( topicAndResult.second.wait_until( timeout ) == std::future_status::timeout )
            {
                FWE_LOG_WARN( "Unsubscribe operation timed out for topic " + topicAndResult.first );
            }
        }
    }

    mInitialConnectionThread.stop();
    for ( auto sender : mSenders )
    {
        sender->invalidateConnection();
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
AwsIotConnectivityModule::createMqttClient()
{
    FWE_LOG_INFO( "Creating MQTT client" );

    if ( mClientId.empty() )
    {
        FWE_LOG_ERROR( "Please provide the client ID" );
        return false;
    }

    // Setup connection options
    auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
    // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
    connectOptions->WithClientId( mClientId.c_str() )
        .WithSessionExpiryIntervalSec( mConnectionConfig.sessionExpiryIntervalSeconds )
        .WithKeepAliveIntervalSec( mConnectionConfig.keepAliveIntervalSeconds );

    if ( mConnectionConfig.sessionExpiryIntervalSeconds > 0 )
    {
        connectOptions->WithSessionExpiryIntervalSec( mConnectionConfig.sessionExpiryIntervalSeconds );
    }

    mMqttClientBuilder
        .WithClientExtendedValidationAndFlowControl(
            Aws::Crt::Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_AWS_IOT_CORE_DEFAULTS )
        // Make queued packets fail on disconnection so we can better control how to handle those failures
        // (e.g. drop, persist). Otherwise, using the default behavior, packets with QoS1 could stay in the
        // queue for a long time and be transmitted even if they are no longer relevant.
        .WithOfflineQueueBehavior(
            Aws::Crt::Mqtt5::ClientOperationQueueBehaviorType::AWS_MQTT5_COQBT_FAIL_ALL_ON_DISCONNECT )
        .WithConnectOptions( connectOptions )
        .WithPingTimeoutMs( mConnectionConfig.pingTimeoutMs );

    if ( mConnectionConfig.sessionExpiryIntervalSeconds > 0 )
    {
        mMqttClientBuilder.WithSessionBehavior(
            Aws::Crt::Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_ALWAYS );
    }

    if ( !mRootCA.empty() )
    {
        mMqttClientBuilder.WithCertificateAuthority( Crt::ByteCursorFromCString( mRootCA.c_str() ) );
    }

    mMqttClientBuilder.WithClientConnectionSuccessCallback(
        [&]( const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData ) {
            std::string logMessage = "Connection completed successfully";
            bool rejoinedSession = false;
            if ( eventData.negotiatedSettings != nullptr )
            {
                rejoinedSession = eventData.negotiatedSettings->getRejoinedSession();
                // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
                auto clientId = std::string( eventData.negotiatedSettings->getClientId().c_str() );
                auto negotiatedKeepAlive = eventData.negotiatedSettings->getServerKeepAlive();
                logMessage += ". ClientId: " + clientId + ", SessionExpiryIntervalSec: " +
                              std::to_string( eventData.negotiatedSettings->getSessionExpiryIntervalSec() ) +
                              ", ServerKeepAliveSec: " + std::to_string( negotiatedKeepAlive ) +
                              ", RejoinedSession: " + ( rejoinedSession ? "true" : "false" );
                if ( negotiatedKeepAlive != mConnectionConfig.keepAliveIntervalSeconds )
                {
                    FWE_LOG_WARN( "Negotiated keep alive " + std::to_string( negotiatedKeepAlive ) +
                                  " does not match the requested value " +
                                  std::to_string( mConnectionConfig.keepAliveIntervalSeconds ) );
                }
            }
            FWE_LOG_INFO( logMessage );
            mConnected = true;
            mConnectionCompletedPromise.set_value( true );
            mConnectionCompletedPromise = std::promise<bool>();
            renameEventLoopTask();

            // If we didn't rejoin a session (which could happen because the previous session expired
            // or persistent session is disabled), the client won't be subscribed to any topic even if
            // this is a reconnection. So we need to ensure that all receivers subscribe again.
            if ( !rejoinedSession )
            {
                std::lock_guard<std::mutex> lock( mTopicsMutex );
                for ( auto &topicAndReceiver : mSubscribedTopicToReceiver )
                {
                    topicAndReceiver.second->resetSubscription();
                }
            }

            if ( mSubscriptionsThread.isAlive() )
            {
                mSubscriptionsThread.restart();
            }
            else
            {
                mSubscriptionsThread.start();
            }
        } );

    mMqttClientBuilder.WithClientConnectionFailureCallback(
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

    mMqttClientBuilder.WithClientAttemptingConnectCallback( [&]( const OnAttemptingConnectEventData &eventData ) {
        static_cast<void>( eventData );
        FWE_LOG_INFO( "Attempting MQTT connection" );
    } );

    mMqttClientBuilder.WithClientDisconnectionCallback( [&]( const OnDisconnectionEventData &eventData ) {
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
    } );

    mMqttClientBuilder.WithClientStoppedCallback( [&]( const Aws::Crt::Mqtt5::OnStoppedEventData &eventData ) {
        static_cast<void>( eventData );
        FWE_LOG_INFO( "The MQTT connection is closed and client stopped" );
        mConnectionClosedPromise.set_value();
        mConnected = false;
        mConnectionEstablished = false;
        mConnectionCompletedPromise = std::promise<bool>();
    } );

    mMqttClientBuilder.WithPublishReceivedCallback( [&]( const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData ) {
        std::ostringstream os;
        os << "Data received on the topic: " << eventData.publishPacket->getTopic()
           << " with a payload length of: " << eventData.publishPacket->getPayload().len;
        FWE_LOG_TRACE( os.str() );

        // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
        auto topic = std::string( eventData.publishPacket->getTopic().c_str() );
        std::shared_ptr<AwsIotReceiver> receiver;
        {
            std::lock_guard<std::mutex> lock( mTopicsMutex );
            receiver = mSubscribedTopicsTree.find( topic );
        }

        if ( receiver == nullptr )
        {
            FWE_LOG_ERROR( "Receiver not found for topic " + topic );
            return;
        }

        receiver->onDataReceived( eventData );
    } );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    mMqttClient = mMqttClientBuilder.Build();
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );
    if ( !mMqttClient )
    {
        int lastError = mMqttClientBuilder.LastError();
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
AwsIotConnectivityModule::connectMqttClient()
{
    FWE_LOG_TRACE( "Starting MQTT client" );

    // Get the future before calling the client code as get_future() is not guaranteed to be thread-safe
    auto connectionResult = mConnectionCompletedPromise.get_future();
    if ( !mMqttClient->Start() )
    {
        int lastError = mMqttClient->LastError();
        auto errorString = Aws::Crt::ErrorDebugString( lastError );
        FWE_LOG_WARN( "The MQTT Connection failed with error  code " + std::to_string( lastError ) + ": " +
                      std::string( errorString != nullptr ? errorString : "Unknown error" ) );
        mConnectionCompletedPromise = std::promise<bool>();
        return RetryStatus::RETRY;
    }

    FWE_LOG_TRACE( "Waiting for connection completed callback" );
    mConnectionEstablished = true;
    // Block until the connection establishes or fails.
    // If the connection fails, the module will also fail.
    if ( !connectionResult.get() )
    {
        // Cleanup resources
        resetConnection();
        return RetryStatus::RETRY;
    }

    return RetryStatus::SUCCESS;
}

RetryStatus
AwsIotConnectivityModule::subscribeAllReceivers()
{
    if ( !isAlive() )
    {
        FWE_LOG_ERROR( "MQTT Connection not established, failed to subscribe" );
        return RetryStatus::RETRY;
    }

    auto result = RetryStatus::SUCCESS;

    // We make a copy because the subscribe() call is blocking and can take very long. So we should
    // not hold the lock the whole time, otherwise we could block the callbacks called by the MQTT
    // client.
    std::vector<std::shared_ptr<AwsIotReceiver>> receivers;
    {
        std::lock_guard<std::mutex> lock( mTopicsMutex );
        for ( auto &topicAndReceiver : mSubscribedTopicToReceiver )
        {
            receivers.emplace_back( topicAndReceiver.second );
        }
    }

    for ( auto receiver : receivers )
    {
        if ( receiver->subscribe() != ConnectivityError::Success )
        {
            result = RetryStatus::RETRY;
        }
    }

    if ( result == RetryStatus::SUCCESS )
    {
        // subscribe to all topics first before notifying listeners for connection
        mConnectionEstablishedListeners.notify();
    }

    return result;
}

AwsIotConnectivityModule::~AwsIotConnectivityModule()
{
    AwsIotConnectivityModule::disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws
