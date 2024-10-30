// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "AwsBootstrap.h"
#include "AwsIotReceiver.h"
#include "AwsIotSender.h"
#include "AwsSDKMemoryManager.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "IConnectionTypes.h"
#include "IReceiver.h"
#include "MqttClientWrapper.h"
#include "MqttClientWrapperMock.h"
#include "WaitUntil.h"
#include <array>
#include <atomic>
#include <aws/common/error.h>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::A;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::StrictMock;

std::string
byteCursorToString( const Aws::Crt::ByteCursor &byteCursor )
{
    return { reinterpret_cast<char *>( byteCursor.ptr ), byteCursor.len };
}

class AwsIotConnectivityModuleTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        // Need to initialize the SDK to get proper error strings
        AwsBootstrap::getInstance().getClientBootStrap();

        mNegotiatedSettings.maximum_qos = AWS_MQTT5_QOS_AT_LEAST_ONCE;
        mNegotiatedSettings.session_expiry_interval = 0;
        mNegotiatedSettings.receive_maximum_from_server = 10000;
        mNegotiatedSettings.maximum_packet_size_to_server = 10000;
        mNegotiatedSettings.topic_alias_maximum_to_server = 50;
        mNegotiatedSettings.topic_alias_maximum_to_client = 80;
        mNegotiatedSettings.server_keep_alive = 60;
        mNegotiatedSettings.retain_available = false;
        mNegotiatedSettings.wildcard_subscriptions_available = false;
        mNegotiatedSettings.subscription_identifiers_available = false;
        mNegotiatedSettings.shared_subscriptions_available = false;
        mNegotiatedSettings.rejoined_session = false;
        std::string clientId = "client-1234";
        auto clientIdByteCursor = Aws::Crt::ByteCursorFromCString( clientId.c_str() );
        aws_mqtt5_negotiated_settings_init( Aws::Crt::ApiAllocator(), &mNegotiatedSettings, &clientIdByteCursor );

        mMqttClientWrapperMock = std::make_shared<StrictMock<MqttClientWrapperMock>>();
        // We need to pass the client shared_ptr to AwsIotSender and AwsIotReceiver as a reference, so we can't pass the
        // pointer to the subclass (i.e. MqttClientWrapperMock).
        mMqttClientWrapper = mMqttClientWrapperMock;
        EXPECT_CALL( *mMqttClientWrapperMock, MockedOperatorBool() )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( true ) );
        ON_CALL( *mMqttClientWrapperMock, Start() ).WillByDefault( Invoke( [this]() noexcept -> bool {
            Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
            mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( eventData );
            return true;
        } ) );
        ON_CALL( *mMqttClientWrapperMock, Stop( _ ) )
            .WillByDefault( Invoke(
                [this]( std::shared_ptr<Aws::Crt::Mqtt5::DisconnectPacket> disconnectOptions ) noexcept -> bool {
                    static_cast<void>( disconnectOptions );
                    Aws::Crt::Mqtt5::OnStoppedEventData eventData;
                    mMqttClientBuilderWrapperMock->mOnStoppedCallback( eventData );
                    return true;
                } ) );
        ON_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) )
            .WillByDefault(
                Invoke( [this]( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket>,
                                Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ) -> bool {
                    onSubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                    mSubscribeCount++;
                    return true;
                } ) );
        ON_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) )
            .WillByDefault( Invoke(
                [this](
                    std::shared_ptr<UnsubscribePacket>,
                    Aws::Crt::Mqtt5::OnUnsubscribeCompletionHandler onUnsubscribeCompletionCallback ) noexcept -> bool {
                    onUnsubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                    return true;
                } ) );

        mMqttClientBuilderWrapperMock = std::make_shared<StrictMock<MqttClientBuilderWrapperMock>>();
        ON_CALL( *mMqttClientBuilderWrapperMock, Build() ).WillByDefault( Return( mMqttClientWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) )
            .WillByDefault( DoAll( SaveArg<0>( &mConnectPacket ), ReturnRef( *mMqttClientBuilderWrapperMock ) ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithCertificateAuthority( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );

        mConnectivityModule =
            std::make_shared<AwsIotConnectivityModule>( "", "clientIdTest", mMqttClientBuilderWrapperMock );
    }

    void
    TearDown() override
    {
        aws_mqtt5_negotiated_settings_clean_up( &mNegotiatedSettings );
    }

    void
    publishToTopic( const std::string &topic, const std::string &data )
    {
        {
            Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

            auto publishPacket =
                std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( topic.c_str(),
                                                                  Aws::Crt::ByteCursorFromCString( data.c_str() ),
                                                                  Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
            eventData.publishPacket = publishPacket;
            mMqttClientBuilderWrapperMock->mOnPublishReceivedHandlerCallback( eventData );
        }
    }

    std::shared_ptr<StrictMock<MqttClientWrapperMock>> mMqttClientWrapperMock;
    std::shared_ptr<MqttClientWrapper> mMqttClientWrapper;
    std::shared_ptr<StrictMock<MqttClientBuilderWrapperMock>> mMqttClientBuilderWrapperMock;
    std::shared_ptr<AwsIotConnectivityModule> mConnectivityModule;
    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> mConnectPacket;
    aws_mqtt5_negotiated_settings mNegotiatedSettings;
    std::atomic<int> mSubscribeCount{ 0 };
};

/**
 * @brief Variation from the main test class to make it easier to setup tests that just need the
 * module to be connected.
 */
class AwsIotConnectivityModuleTestAfterSuccessfulConnection : public AwsIotConnectivityModuleTest
{
protected:
    void
    SetUp() override
    {
        AwsIotConnectivityModuleTest::SetUp();

        EXPECT_CALL(
            *mMqttClientBuilderWrapperMock,
            WithClientExtendedValidationAndFlowControl(
                Aws::Crt::Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_AWS_IOT_CORE_DEFAULTS ) )
            .Times( 1 );
        EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
        EXPECT_CALL( *mMqttClientBuilderWrapperMock,
                     WithOfflineQueueBehavior(
                         Aws::Crt::Mqtt5::ClientOperationQueueBehaviorType::AWS_MQTT5_COQBT_FAIL_ALL_ON_DISCONNECT ) )
            .Times( 1 );
        EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( MQTT_PING_TIMEOUT_MS ) ).Times( 1 );
        EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
        EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

        ASSERT_TRUE( mConnectivityModule->connect() );

        WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    }

    void
    TearDown() override
    {
        AwsIotConnectivityModuleTest::TearDown();
        // This should be called on AwsIotConnectivityModule destruction
        EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );
    }
};

TEST_F( AwsIotConnectivityModuleTest, failToConnectWhenBuilderIsInvalid )
{
    mConnectivityModule = std::make_shared<AwsIotConnectivityModule>( "fakeRootCA", "clientIdTest", nullptr );
    ASSERT_FALSE( mConnectivityModule->connect() );
}

/** @brief  Test attempting to disconnect when connection has already failed */
TEST_F( AwsIotConnectivityModuleTest, disconnectAfterFailedConnect )
{
    mConnectivityModule = std::make_shared<AwsIotConnectivityModule>( "", "", mMqttClientBuilderWrapperMock );
    ASSERT_FALSE( mConnectivityModule->connect() );
    // disconnect must only disconnect when connection is available so this should not seg fault
    mConnectivityModule->disconnect();
}

/** @brief Test successful connection */
TEST_F( AwsIotConnectivityModuleTest, connectSuccessfully )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( MQTT_PING_TIMEOUT_MS ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );
    ASSERT_EQ( mConnectPacket->getKeepAliveIntervalSec(), MQTT_KEEP_ALIVE_INTERVAL_SECONDS );
    ASSERT_EQ( mConnectPacket->getSessionExpiryIntervalSec().value(), MQTT_SESSION_EXPIRY_INTERVAL_SECONDS );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

/** @brief Test successful connection with root CA */
TEST_F( AwsIotConnectivityModuleTest, connectSuccessfullyWithRootCA )
{
    mConnectivityModule =
        std::make_shared<AwsIotConnectivityModule>( "fakeRootCA", "clientIdTest", mMqttClientBuilderWrapperMock );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithCertificateAuthority( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );
}

TEST_F( AwsIotConnectivityModuleTest, connectSuccessfullyWithOverridenConnectionConfig )
{
    AwsIotConnectivityConfig mqttConnectionConfig;
    mqttConnectionConfig.keepAliveIntervalSeconds = 321;
    mqttConnectionConfig.pingTimeoutMs = 17;

    mConnectivityModule = std::make_shared<AwsIotConnectivityModule>(
        "", "clientIdTest", mMqttClientBuilderWrapperMock, mqttConnectionConfig );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( 17 ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );
    ASSERT_EQ( mConnectPacket->getKeepAliveIntervalSec(), 321 );
    ASSERT_EQ( mConnectPacket->getSessionExpiryIntervalSec().value(), 0 );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );
}

TEST_F( AwsIotConnectivityModuleTest, connectSuccessfullyWithPersistentSession )
{
    AwsIotConnectivityConfig mqttConnectionConfig;
    mqttConnectionConfig.sessionExpiryIntervalSeconds = 7890;

    mConnectivityModule = std::make_shared<AwsIotConnectivityModule>(
        "", "clientIdTest", mMqttClientBuilderWrapperMock, mqttConnectionConfig );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( AWS_MQTT5_CSBT_REJOIN_ALWAYS ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( MQTT_PING_TIMEOUT_MS ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );
    ASSERT_EQ( mConnectPacket->getKeepAliveIntervalSec(), MQTT_KEEP_ALIVE_INTERVAL_SECONDS );
    ASSERT_EQ( mConnectPacket->getSessionExpiryIntervalSec().value(), 7890 );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );
}

/** @brief Test trying to connect, where creation of the client fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientCreation )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, MockedOperatorBool() )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( Return( false ) );
    EXPECT_CALL( *mMqttClientWrapperMock, LastError() ).WillOnce( Return( 1 ) );

    ASSERT_FALSE( mConnectivityModule->connect() );
}

/** @brief Test opening a connection, then interrupting it and resuming it */
TEST_F( AwsIotConnectivityModuleTest, connectionInterrupted )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    {
        Aws::Crt::Mqtt5::OnDisconnectionEventData eventData;
        eventData.errorCode = AWS_ERROR_MQTT_UNEXPECTED_HANGUP;
        mMqttClientBuilderWrapperMock->mOnDisconnectionCallback( eventData );
    }

    WAIT_ASSERT_FALSE( mConnectivityModule->isAlive() );

    {
        Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
        mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( eventData );
    }

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
}

TEST_F( AwsIotConnectivityModuleTest, clientFailsToStartFirstAttempt )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    {
        Sequence seq;
        EXPECT_CALL( *mMqttClientWrapperMock, Start() ).InSequence( seq ).WillOnce( Invoke( [this]() noexcept -> bool {
            Aws::Crt::Mqtt5::OnConnectionFailureEventData eventData;
            mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback( eventData );
            return false;
        } ) );
        EXPECT_CALL( *mMqttClientWrapperMock, Start() ).InSequence( seq ).WillOnce( Invoke( [this]() noexcept -> bool {
            Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
            mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( eventData );
            return true;
        } ) );
    }
    EXPECT_CALL( *mMqttClientWrapperMock, LastError() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
}

/** @brief Test connecting when it fails after a delay */
TEST_F( AwsIotConnectivityModuleTest, connectFailsServerUnavailableWithDelay )
{
    std::atomic<bool> killAllThread( false );
    std::promise<void> completed;
    std::thread completeThread( [this, &killAllThread, &completed]() {
        while ( mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback == nullptr && !killAllThread )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        Aws::Crt::Mqtt5::OnConnectionFailureEventData eventData;
        eventData.errorCode = AWS_ERROR_MQTT_TIMEOUT;
        mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback( eventData );
        completed.set_value();
    } );

    std::thread disconnectThread( [this, &killAllThread, &completed]() {
        while ( mMqttClientBuilderWrapperMock->mOnDisconnectionCallback == nullptr && !killAllThread )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        completed.get_future().wait();
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        mMqttClientBuilderWrapperMock->mOnDisconnectionCallback( Aws::Crt::Mqtt5::OnDisconnectionEventData() );
    } );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    // Don't automatically invoke the callback, we want to call it manually.
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );
    // We want to see exactly one call to disconnect
    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    killAllThread = true;
    completeThread.join();
    disconnectThread.join();

    ASSERT_FALSE( mConnectivityModule->isAlive() );

    // Don't expect more calls on destruction since it is already disconnected
    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 0 );
}

/** @brief Test subscribing without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutTopic )
{
    AwsIotReceiver receiver( mConnectivityModule.get(), mMqttClientWrapper, "" );
    ASSERT_EQ( receiver.subscribe(), ConnectivityError::NotConfigured );
    receiver.invalidateConnection();
}

/** @brief Test subscribing without being connected, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutBeingConnected )
{
    AwsIotReceiver receiver( mConnectivityModule.get(), mMqttClientWrapper, "topic" );
    ASSERT_EQ( receiver.subscribe(), ConnectivityError::NoConnection );
    receiver.invalidateConnection();
}

TEST_F( AwsIotConnectivityModuleTest, receiveMessage )
{
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver1;
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver2;
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver3;

    auto receiver1 = mConnectivityModule->createReceiver( "topic1" );
    auto receiver2 = mConnectivityModule->createReceiver( "topic2" );
    auto receiver3 = mConnectivityModule->createReceiver( "topic3/+/request" );

    receiver1->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    receiver2->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver2.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    receiver3->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver3.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 3 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver1.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver2.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver3.get() )->isAlive() );

    auto publishTime = ClockHandler::getClock()->monotonicTimeSinceEpochMs();
    // Simulate messages coming from MQTT client
    publishToTopic( "topic1", "data1" );
    publishToTopic( "topic2", "data2" );
    publishToTopic( "topic3/12345/request", "data3" );

    ASSERT_EQ( receivedDataReceiver1.size(), 1 );
    ASSERT_EQ( receivedDataReceiver1[0].first, "data1" );
    ASSERT_GE( receivedDataReceiver1[0].second.receivedMonotonicTimeMs, publishTime );
    // Give some margin for error due to test being slow, but make sure that timeout is not much more
    // than expected.
    ASSERT_LE( receivedDataReceiver1[0].second.receivedMonotonicTimeMs, publishTime + 500 );

    ASSERT_EQ( receivedDataReceiver2.size(), 1 );
    ASSERT_EQ( receivedDataReceiver2[0].first, "data2" );

    ASSERT_EQ( receivedDataReceiver3.size(), 1 );
    ASSERT_EQ( receivedDataReceiver3[0].first, "data3" );

    // Should be called on destruction
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) ).Times( 3 );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

TEST_F( AwsIotConnectivityModuleTest, retryFailedSubscription )
{
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver1;
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver2;

    auto receiver1 = mConnectivityModule->createReceiver( "topic1" );
    auto receiver2 = mConnectivityModule->createReceiver( "topic2" );

    receiver1->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    receiver2->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver2.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    {
        Sequence seq;
        EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 1 ).InSequence( seq );
        // One of the subscriptions will fail
        EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) )
            .InSequence( seq )
            .WillOnce(
                Invoke( [this]( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket>,
                                Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ) -> bool {
                    onSubscribeCompletionCallback( AWS_ERROR_MQTT_TIMEOUT, nullptr );
                    return true;
                } ) );
        // Then on retry, we should expect only one additional Subscribe call (the topic that succeeded
        // should not be re-subscribed)
        EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 1 ).InSequence( seq );
    }

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver1.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver2.get() )->isAlive() );

    // Simulate messages coming from MQTT client
    publishToTopic( "topic1", "data1" );
    publishToTopic( "topic2", "data2" );

    ASSERT_EQ( receivedDataReceiver1.size(), 1 );
    ASSERT_EQ( receivedDataReceiver1[0].first, "data1" );

    ASSERT_EQ( receivedDataReceiver2.size(), 1 );
    ASSERT_EQ( receivedDataReceiver2[0].first, "data2" );

    // Should be called on destruction
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) ).Times( 2 );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

TEST_F( AwsIotConnectivityModuleTest, resubscribeToAllTopicsWhenNotRejoiningExistingSession )
{
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver1;
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver2;

    auto receiver1 = mConnectivityModule->createReceiver( "topic1" );
    auto receiver2 = mConnectivityModule->createReceiver( "topic2" );

    receiver1->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    receiver2->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver2.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 2 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    WAIT_ASSERT_EQ( mSubscribeCount.load(), 2 );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver1.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver2.get() )->isAlive() );

    // Simulate a reconnection. Note that when using the real client, once we start the client,
    // both failure and success callbacks will be called without requiring any action from our side.
    mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback( Aws::Crt::Mqtt5::OnConnectionFailureEventData() );
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 2 );
    {
        mNegotiatedSettings.rejoined_session = false;
        Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
        eventData.negotiatedSettings = std::make_shared<Aws::Crt::Mqtt5::NegotiatedSettings>( mNegotiatedSettings );
        mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( eventData );
    }
    WAIT_ASSERT_EQ( mSubscribeCount.load(), 4 );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver1.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver2.get() )->isAlive() );

    // Simulate messages coming from MQTT client
    publishToTopic( "topic1", "data1" );
    publishToTopic( "topic2", "data2" );

    ASSERT_EQ( receivedDataReceiver1.size(), 1 );
    ASSERT_EQ( receivedDataReceiver1[0].first, "data1" );

    ASSERT_EQ( receivedDataReceiver2.size(), 1 );
    ASSERT_EQ( receivedDataReceiver2[0].first, "data2" );

    // Should be called on destruction
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) ).Times( 2 );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

TEST_F( AwsIotConnectivityModuleTest, resubscribeOnlyToFailedTopicsWhenRejoiningExistingSession )
{
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver1;
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver2;

    auto receiver1 = mConnectivityModule->createReceiver( "topic1" );
    auto receiver2 = mConnectivityModule->createReceiver( "topic2" );

    receiver1->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    receiver2->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver2.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    // One of the subscriptions will fail
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) )
        .Times( AnyNumber() )
        .WillRepeatedly(
            Invoke( [this]( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket> subscribePacket,
                            Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ) -> bool {
                aws_mqtt5_packet_subscribe_view rawSubscribeOptions;
                subscribePacket->initializeRawOptions( rawSubscribeOptions );
                if ( byteCursorToString( rawSubscribeOptions.subscriptions[0].topic_filter ) == "topic1" )
                {
                    onSubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                }
                else
                {
                    onSubscribeCompletionCallback( AWS_ERROR_MQTT_TIMEOUT, nullptr );
                }
                mSubscribeCount++;
                return true;
            } ) );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    WAIT_ASSERT_EQ( mSubscribeCount.load(), 2 );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver1.get() )->isAlive() );
    ASSERT_FALSE( static_cast<IReceiver *>( receiver2.get() )->isAlive() );

    // Simulate a reconnection. Note that when using the real client, once we start the client,
    // both failure and success callbacks will be called without requiring any action from our side.
    mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback( Aws::Crt::Mqtt5::OnConnectionFailureEventData() );
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 1 );
    {
        mNegotiatedSettings.rejoined_session = true;
        Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
        eventData.negotiatedSettings = std::make_shared<Aws::Crt::Mqtt5::NegotiatedSettings>( mNegotiatedSettings );
        mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( eventData );
    }

    WAIT_ASSERT_EQ( mSubscribeCount.load(), 3 );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver1.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( receiver2.get() )->isAlive() );

    // Simulate messages coming from MQTT client
    publishToTopic( "topic1", "data1" );
    publishToTopic( "topic2", "data2" );

    ASSERT_EQ( receivedDataReceiver1.size(), 1 );
    ASSERT_EQ( receivedDataReceiver1[0].first, "data1" );

    ASSERT_EQ( receivedDataReceiver2.size(), 1 );
    ASSERT_EQ( receivedDataReceiver2[0].first, "data2" );

    // Should be called on destruction
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) ).Times( 2 );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, receiveMessageFromTopicWithNoReceiver )
{
    // Simulate messages coming from MQTT client
    std::string data1 = "data1";
    Aws::Crt::Mqtt5::PublishReceivedEventData eventData;
    auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        "topic1", Aws::Crt::ByteCursorFromCString( data1.c_str() ), Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
    eventData.publishPacket = publishPacket;
    mMqttClientBuilderWrapperMock->mOnPublishReceivedHandlerCallback( eventData );
    // This situation shouldn't normally happen as we won't receive messages from a topic that we
    // didn't subscribe to. But we need to make sure we won't crash if that happens.
}

/** @brief Test successful subscription */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, subscribeSuccessfully )
{
    AwsIotReceiver receiver( mConnectivityModule.get(), mMqttClientWrapper, "topic" );

    std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket> subscribeOptions;
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) )
        .Times( 1 )
        .WillRepeatedly(
            Invoke( [this, &subscribeOptions](
                        std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket> options,
                        Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ) -> bool {
                subscribeOptions = options;
                int errorCode = AWS_ERROR_SUCCESS;
                onSubscribeCompletionCallback( errorCode, nullptr );
                return true;
            } ) );
    ASSERT_EQ( receiver.subscribe(), ConnectivityError::Success );
    aws_mqtt5_packet_subscribe_view rawSubscribeOptions;
    subscribeOptions->initializeRawOptions( rawSubscribeOptions );
    ASSERT_EQ( rawSubscribeOptions.subscription_count, 1 );
    ASSERT_EQ( byteCursorToString( rawSubscribeOptions.subscriptions[0].topic_filter ), "topic" );

    std::shared_ptr<UnsubscribePacket> unsubscribeOptions;
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( Invoke(
            [this, &unsubscribeOptions](
                std::shared_ptr<UnsubscribePacket> options,
                Aws::Crt::Mqtt5::OnUnsubscribeCompletionHandler onUnsubscribeCompletionCallback ) noexcept -> bool {
                unsubscribeOptions = options;
                int errorCode = AWS_ERROR_SUCCESS;
                onUnsubscribeCompletionCallback( errorCode, nullptr );
                return true;
            } ) );
    receiver.unsubscribe();
    aws_mqtt5_packet_unsubscribe_view rawUnsubscribeOptions;
    unsubscribeOptions->initializeRawOptions( rawUnsubscribeOptions );
    ASSERT_EQ( rawUnsubscribeOptions.topic_filter_count, 1 );
    ASSERT_EQ( byteCursorToString( rawUnsubscribeOptions.topic_filters[0] ), "topic" );

    receiver.invalidateConnection();
}

/** @brief Test without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutTopic )
{
    AwsIotSender sender(
        mConnectivityModule.get(), mMqttClientWrapper, "", Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );

    std::uint8_t input[] = { 0xca, 0xfe };
    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NotConfigured ) ).Times( 1 );
    sender.sendBuffer( input, sizeof( input ), resultCallback.AsStdFunction() );
    sender.invalidateConnection();
}

/** @brief Test sending without a connection, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutConnection )
{
    AwsIotSender sender(
        mConnectivityModule.get(), mMqttClientWrapper, "topic", Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NoConnection ) ).Times( 1 );
    std::uint8_t input[] = { 0xca, 0xfe };
    sender.sendBuffer( input, sizeof( input ), resultCallback.AsStdFunction() );
    sender.invalidateConnection();
}

/** @brief Test passing a null pointer, expect an error */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendWrongInput )
{
    AwsIotSender sender(
        mConnectivityModule.get(), mMqttClientWrapper, "topic", Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::WrongInputData ) ).Times( 1 );
    sender.sendBuffer( nullptr, 10, resultCallback.AsStdFunction() );
    sender.invalidateConnection();
}

/** @brief Test sending a message larger then the maximum send size, expect an error */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendTooBig )
{
    AwsIotSender sender(
        mConnectivityModule.get(), mMqttClientWrapper, "topic", Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::WrongInputData ) ).Times( 1 );
    std::vector<uint8_t> a;
    a.resize( sender.getMaxSendSize() + 1U );
    sender.sendBuffer( a.data(), a.size(), resultCallback.AsStdFunction() );
    sender.invalidateConnection();
}

/** @brief Test sending multiple messages. The API supports queuing of messages, so send more than one
 * message, allow one to be sent, queue another and then send the rest. Also indicate one of
 * messages as failed to send to check that path. */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendMultiple )
{
    AwsIotSender sender(
        mConnectivityModule.get(), mMqttClientWrapper, "topic", Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );

    std::uint8_t input[] = { 0xca, 0xfe };
    std::list<Aws::Crt::Mqtt5::OnPublishCompletionHandler> completeHandlers;
    EXPECT_CALL( *mMqttClientWrapperMock, Publish( _, _ ) )
        .Times( 3 )
        .WillRepeatedly(
            Invoke( [&completeHandlers](
                        std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket>,
                        Aws::Crt::Mqtt5::OnPublishCompletionHandler onPublishCompletionCallback ) noexcept -> bool {
                completeHandlers.push_back( std::move( onPublishCompletionCallback ) );
                return true;
            } ) );

    // Queue 2 packets
    MockFunction<void( ConnectivityError result )> resultCallback1;
    sender.sendBuffer( input, sizeof( input ), resultCallback1.AsStdFunction() );
    MockFunction<void( ConnectivityError result )> resultCallback2;
    sender.sendBuffer( input, sizeof( input ), resultCallback2.AsStdFunction() );

    // Confirm 1st
    EXPECT_CALL( resultCallback1, Call( ConnectivityError::Success ) ).Times( 1 );
    completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
    completeHandlers.pop_front();

    // Queue another:
    MockFunction<void( ConnectivityError result )> resultCallback3;
    sender.sendBuffer( input, sizeof( input ), resultCallback3.AsStdFunction() );

    // Confirm 2nd
    EXPECT_CALL( resultCallback2, Call( ConnectivityError::Success ) ).Times( 1 );
    completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
    completeHandlers.pop_front();
    // Confirm 3rd (Not a test case failure, but a stimulated failure for code coverage)
    EXPECT_CALL( resultCallback3, Call( ConnectivityError::TransmissionError ) ).Times( 1 );
    completeHandlers.front().operator()( 1, std::make_shared<PublishResult>( 1 ) );
    completeHandlers.pop_front();

    ASSERT_EQ( sender.getPayloadCountSent(), 2 );

    sender.invalidateConnection();
}

/** @brief Test SDK exceeds RAM and Sender stops sending */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sdkRAMExceeded )
{
    auto &memMgr = AwsSDKMemoryManager::getInstance();

    AwsIotSender sender(
        mConnectivityModule.get(), mMqttClientWrapper, "topic", Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );

    std::array<std::uint8_t, 2> input = { 0xCA, 0xFE };
    const auto required = input.size() * sizeof( std::uint8_t );
    {
        auto reservedMemory = AwsSDKMemoryManager::getInstance().getLimit();
        ASSERT_TRUE( memMgr.reserveMemory( reservedMemory ) );

        MockFunction<void( ConnectivityError result )> resultCallback1;
        EXPECT_CALL( resultCallback1, Call( ConnectivityError::QuotaReached ) ).Times( 1 );
        sender.sendBuffer( input.data(), input.size() * sizeof( std::uint8_t ), resultCallback1.AsStdFunction() );
        ASSERT_EQ( memMgr.releaseReservedMemory( reservedMemory ), 0 );
    }
    {
        // check that allocation and hence send succeed when there is just enough memory
        // here we subtract the offset twice - once for MAXIMUM_AWS_SDK_HEAP_MEMORY_BYTES and once for the input
        auto reservedMemory = AwsSDKMemoryManager::getInstance().getLimit() - required;
        ASSERT_TRUE( memMgr.reserveMemory( reservedMemory ) );

        std::list<Aws::Crt::Mqtt5::OnPublishCompletionHandler> completeHandlers;
        EXPECT_CALL( *mMqttClientWrapperMock, Publish( _, _ ) )
            .Times( AnyNumber() )
            .WillRepeatedly(
                Invoke( [&completeHandlers](
                            std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket>,
                            Aws::Crt::Mqtt5::OnPublishCompletionHandler onPublishCompletionCallback ) noexcept -> bool {
                    completeHandlers.push_back( std::move( onPublishCompletionCallback ) );
                    return true;
                } ) );

        MockFunction<void( ConnectivityError result )> resultCallback3;
        sender.sendBuffer( input.data(), sizeof( input ), resultCallback3.AsStdFunction() );

        // // Confirm 1st
        EXPECT_CALL( resultCallback3, Call( ConnectivityError::Success ) ).Times( 1 );
        completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
        completeHandlers.pop_front();

        ASSERT_EQ( memMgr.releaseReservedMemory( reservedMemory ), 0 );
    }

    sender.invalidateConnection();
}

/** @brief Test the separate thread with exponential backoff that tries to connect until connection succeeds */
TEST_F( AwsIotConnectivityModuleTest, asyncConnect )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    // Don't automatically invoke the callback, we want to call it manually.
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );

    ASSERT_TRUE( mConnectivityModule->connect() );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // first attempt should come immediately

    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );
    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).WillOnce( Return( true ) );
    {
        Aws::Crt::Mqtt5::OnConnectionFailureEventData eventData;
        eventData.errorCode = AWS_ERROR_MQTT_TIMEOUT;
        mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback( eventData );
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    {
        Aws::Crt::Mqtt5::OnStoppedEventData eventData;
        mMqttClientBuilderWrapperMock->mOnStoppedCallback( eventData );
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) ); // minimum wait time 1 second

    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );
    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).WillOnce( Return( true ) );
    {
        Aws::Crt::Mqtt5::OnConnectionFailureEventData eventData;
        eventData.errorCode = AWS_ERROR_MQTT_TIMEOUT;
        mMqttClientBuilderWrapperMock->mOnConnectionFailureCallback( eventData );
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    {
        Aws::Crt::Mqtt5::OnStoppedEventData eventData;
        mMqttClientBuilderWrapperMock->mOnStoppedCallback( eventData );
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 2100 ) ); // exponential backoff now 2 seconds

    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 0 );
    {
        mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( Aws::Crt::Mqtt5::OnConnectionSuccessEventData() );
    }

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
