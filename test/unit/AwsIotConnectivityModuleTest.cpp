// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "AwsBootstrap.h"
#include "AwsIotChannel.h"
#include "AwsSDKMemoryManager.h"
#include "CacheAndPersist.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "IConnectionTypes.h"
#include "IConnectivityChannel.h"
#include "IReceiver.h"
#include "ISender.h"
#include "MqttClientWrapper.h"
#include "MqttClientWrapperMock.h"
#include "PayloadManager.h"
#include "WaitUntil.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <aws/common/error.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
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

        mMqttClientWrapperMock = std::make_shared<StrictMock<MqttClientWrapperMock>>();
        // We need to pass the client shared_ptr to AwsIotChannel as a reference, so we can't pass the pointer to the
        // subclass (i.e. MqttClientWrapperMock).
        mMqttClientWrapper = mMqttClientWrapperMock;
        EXPECT_CALL( *mMqttClientWrapperMock, MockedOperatorBool() )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( true ) );
        ON_CALL( *mMqttClientWrapperMock, Start() ).WillByDefault( Invoke( [this]() noexcept -> bool {
            Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
            mMqttClientBuilderWrapperMock->mOnConnectionSuccessCallback( eventData );
            return true;
        } ) );
        ON_CALL( *mMqttClientWrapperMock, Stop() ).WillByDefault( Invoke( [this]() noexcept -> bool {
            Aws::Crt::Mqtt5::OnStoppedEventData eventData;
            mMqttClientBuilderWrapperMock->mOnStoppedCallback( eventData );
            return true;
        } ) );

        mMqttClientBuilderWrapperMock = std::make_shared<StrictMock<MqttClientBuilderWrapperMock>>();
        ON_CALL( *mMqttClientBuilderWrapperMock, Build() ).WillByDefault( Return( mMqttClientWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) )
            .WillByDefault( DoAll( SaveArg<0>( &mConnectPacket ), ReturnRef( *mMqttClientBuilderWrapperMock ) ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );
        ON_CALL( *mMqttClientBuilderWrapperMock, WithCertificateAuthority( _ ) )
            .WillByDefault( ReturnRef( *mMqttClientBuilderWrapperMock ) );

        mConnectivityModule =
            std::make_shared<AwsIotConnectivityModule>( "", "clientIdTest", mMqttClientBuilderWrapperMock );
    }

    std::shared_ptr<StrictMock<MqttClientWrapperMock>> mMqttClientWrapperMock;
    std::shared_ptr<MqttClientWrapper> mMqttClientWrapper;
    std::shared_ptr<StrictMock<MqttClientBuilderWrapperMock>> mMqttClientBuilderWrapperMock;
    std::shared_ptr<AwsIotConnectivityModule> mConnectivityModule;
    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> mConnectPacket;
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

        EXPECT_CALL( *mMqttClientBuilderWrapperMock,
                     WithClientExtendedValidationAndFlowControl(
                         Aws::Crt::Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_NONE ) )
            .Times( 1 );
        EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
        EXPECT_CALL(
            *mMqttClientBuilderWrapperMock,
            WithSessionBehavior( Aws::Crt::Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS ) )
            .Times( 1 );
        EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( 3000 ) ).Times( 1 );
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
        EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );
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
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

/** @brief Test successful connection with root CA */
TEST_F( AwsIotConnectivityModuleTest, connectSuccessfullyWithRootCA )
{
    mConnectivityModule =
        std::make_shared<AwsIotConnectivityModule>( "fakeRootCA", "clientIdTest", mMqttClientBuilderWrapperMock );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithCertificateAuthority( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );
}

/** @brief Test trying to connect, where creation of the client fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientCreation )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
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
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );

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
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
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
    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );

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
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    // Don't automatically invoke the callback, we want to call it manually.
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );
    // We want to see exactly one call to disconnect
    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->connect() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    killAllThread = true;
    completeThread.join();
    disconnectThread.join();

    ASSERT_FALSE( mConnectivityModule->isAlive() );

    // Don't expect more calls on destruction since it is already disconnected
    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 0 );
}

/** @brief Test subscribing without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutTopic )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "", false );
    ASSERT_EQ( channel.subscribe(), ConnectivityError::NotConfigured );
    channel.invalidateConnection();
}

/** @brief Test subscribing without being connected, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutBeingConnected )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );
    ASSERT_EQ( channel.subscribe(), ConnectivityError::NoConnection );
    channel.invalidateConnection();
}

TEST_F( AwsIotConnectivityModuleTest, receiveMessage )
{
    std::vector<std::pair<std::string, ReceivedChannelMessage>> receivedDataChannel1;
    std::vector<std::pair<std::string, ReceivedChannelMessage>> receivedDataChannel2;

    auto channel1 = mConnectivityModule->createNewChannel( nullptr, "topic1", true );
    auto channel2 = mConnectivityModule->createNewChannel( nullptr, "topic2", true );

    channel1->subscribeToDataReceived( [&]( const ReceivedChannelMessage &message ) {
        receivedDataChannel1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    channel2->subscribeToDataReceived( [&]( const ReceivedChannelMessage &message ) {
        receivedDataChannel2.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) )
        .Times( 2 )
        .WillRepeatedly(
            Invoke( [this]( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket>,
                            Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ) -> bool {
                onSubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                return true;
            } ) );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( channel1.get() )->isAlive() );
    WAIT_ASSERT_TRUE( static_cast<IReceiver *>( channel2.get() )->isAlive() );

    // Simulate messages coming from MQTT client
    std::string data1 = "data1";
    uint64_t expectedMessageExpiryMonotonicTimeSinceEpochMs = UINT64_MAX;
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "topic1",
                                                              Aws::Crt::ByteCursorFromCString( data1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        auto messageExpiryIntervalSec = 5;
        publishPacket->WithMessageExpiryIntervalSec( messageExpiryIntervalSec );
        eventData.publishPacket = publishPacket;
        expectedMessageExpiryMonotonicTimeSinceEpochMs =
            ClockHandler::getClock()->monotonicTimeSinceEpochMs() + ( messageExpiryIntervalSec * 1000 );
        mMqttClientBuilderWrapperMock->mOnPublishReceivedHandlerCallback( eventData );
    }
    std::string data2 = "data2";
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;
        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "topic2",
                                                              Aws::Crt::ByteCursorFromCString( data2.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock->mOnPublishReceivedHandlerCallback( eventData );
    }

    ASSERT_EQ( receivedDataChannel1.size(), 1 );
    ASSERT_EQ( receivedDataChannel1[0].first, "data1" );
    ASSERT_GE( receivedDataChannel1[0].second.messageExpiryMonotonicTimeSinceEpochMs,
               expectedMessageExpiryMonotonicTimeSinceEpochMs );
    // Give some margin for error due to test being slow, but make sure that timeout is not much more
    // than expected.
    ASSERT_LE( receivedDataChannel1[0].second.messageExpiryMonotonicTimeSinceEpochMs,
               expectedMessageExpiryMonotonicTimeSinceEpochMs + 500 );

    ASSERT_EQ( receivedDataChannel2.size(), 1 );
    ASSERT_EQ( receivedDataChannel2[0].first, "data2" );
    ASSERT_EQ( receivedDataChannel2[0].second.messageExpiryMonotonicTimeSinceEpochMs, 0 );

    // Should be called on destruction
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) )
        .Times( 2 )
        .WillRepeatedly( Invoke(
            [this]( std::shared_ptr<UnsubscribePacket>,
                    Aws::Crt::Mqtt5::OnUnsubscribeCompletionHandler onUnsubscribeCompletionCallback ) noexcept -> bool {
                onUnsubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                return true;
            } ) );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, receiveMessageFromTopicWithNoChannel )
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
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );

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
    ASSERT_EQ( channel.subscribe(), ConnectivityError::Success );
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
    channel.unsubscribe();
    aws_mqtt5_packet_unsubscribe_view rawUnsubscribeOptions;
    unsubscribeOptions->initializeRawOptions( rawUnsubscribeOptions );
    ASSERT_EQ( rawUnsubscribeOptions.topic_filter_count, 1 );
    ASSERT_EQ( byteCursorToString( rawUnsubscribeOptions.topic_filters[0] ), "topic" );

    channel.invalidateConnection();
}

/** @brief Test without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutTopic )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "", false );

    std::uint8_t input[] = { 0xca, 0xfe };
    ASSERT_EQ( channel.sendBuffer( input, sizeof( input ) ), ConnectivityError::NotConfigured );
    channel.invalidateConnection();
}

/** @brief Test sending without a connection, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutConnection )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );
    std::uint8_t input[] = { 0xca, 0xfe };
    ASSERT_EQ( channel.sendBuffer( input, sizeof( input ) ), ConnectivityError::NoConnection );
    channel.invalidateConnection();
}

/** @brief Test passing a null pointer, expect an error */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendWrongInput )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );

    ASSERT_EQ( channel.sendBuffer( nullptr, 10 ), ConnectivityError::WrongInputData );
    channel.invalidateConnection();
}

/** @brief Test sending a message larger then the maximum send size, expect an error */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendTooBig )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );

    std::vector<uint8_t> a;
    a.resize( channel.getMaxSendSize() + 1U );
    ASSERT_EQ( channel.sendBuffer( a.data(), a.size() ), ConnectivityError::WrongInputData );
    channel.invalidateConnection();
}

/** @brief Test sending multiple messages. The API supports queuing of messages, so send more than one
 * message, allow one to be sent, queue another and then send the rest. Also indicate one of
 * messages as failed to send to check that path. */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendMultiple )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );

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
    ASSERT_EQ( channel.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );
    ASSERT_EQ( channel.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 1st
    completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
    completeHandlers.pop_front();

    // Queue another:
    ASSERT_EQ( channel.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 2nd
    completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
    completeHandlers.pop_front();
    // Confirm 3rd (Not a test case failure, but a stimulated failure for code coverage)
    completeHandlers.front().operator()( 1, std::make_shared<PublishResult>( 1 ) );
    completeHandlers.pop_front();

    ASSERT_EQ( channel.getPayloadCountSent(), 2 );

    channel.invalidateConnection();
}

/** @brief Test SDK exceeds RAM and Channel stops sending */
TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sdkRAMExceeded )
{
    auto &memMgr = AwsSDKMemoryManager::getInstance();
    void *alloc1 = memMgr.AllocateMemory( 600000000, alignof( std::size_t ) );
    ASSERT_NE( alloc1, nullptr );
    memMgr.FreeMemory( alloc1 );

    void *alloc2 = memMgr.AllocateMemory( 600000010, alignof( std::size_t ) );
    ASSERT_NE( alloc2, nullptr );
    memMgr.FreeMemory( alloc2 );

    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );

    std::array<std::uint8_t, 2> input = { 0xCA, 0xFE };
    const auto required = input.size() * sizeof( std::uint8_t );
    {
        void *alloc3 =
            memMgr.AllocateMemory( 50 * AwsSDKMemoryManager::getInstance().getLimit(), alignof( std::size_t ) );
        ASSERT_NE( alloc3, nullptr );

        ASSERT_EQ( channel.sendBuffer( input.data(), input.size() * sizeof( std::uint8_t ) ),
                   ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc3 );
    }
    {
        constexpr auto offset = alignof( std::max_align_t );
        // check that we will be out of memory even if we allocate less than the max because of the allocator's offset
        // in the below alloc we are leaving space for the input
        auto alloc4 = memMgr.AllocateMemory( AwsSDKMemoryManager::getInstance().getLimit() - ( offset + required ),
                                             alignof( std::size_t ) );
        ASSERT_NE( alloc4, nullptr );
        ASSERT_EQ( channel.sendBuffer( input.data(), sizeof( input ) ), ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc4 );

        // check that allocation and hence send succeed when there is just enough memory
        // here we subtract the offset twice - once for MAXIMUM_AWS_SDK_HEAP_MEMORY_BYTES and once for the input
        auto alloc5 = memMgr.AllocateMemory(
            AwsSDKMemoryManager::getInstance().getLimit() - ( ( 2 * offset ) + required ), alignof( std::size_t ) );
        ASSERT_NE( alloc5, nullptr );

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

        ASSERT_EQ( channel.sendBuffer( input.data(), sizeof( input ) ), ConnectivityError::Success );
        memMgr.FreeMemory( alloc5 );

        // // Confirm 1st
        completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
        completeHandlers.pop_front();
    }

    channel.invalidateConnection();
}

/** @brief Test sending file over MQTT without topic */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoTopic )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "", false );
    std::string filename{ "testFile.json" };
    ASSERT_EQ( channel.sendFile( filename, 0 ), ConnectivityError::NotConfigured );
    channel.invalidateConnection();
}

/** @brief Test sending file over MQTT, payload manager not defined */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoPayloadManager )
{
    AwsIotChannel channel( mConnectivityModule.get(), nullptr, mMqttClientWrapper, "topic", false );
    std::string filename{ "testFile.json" };
    ASSERT_EQ( channel.sendFile( filename, 0 ), ConnectivityError::NotConfigured );
    channel.invalidateConnection();
}

/** @brief Test sending file over MQTT, filename not defined */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoFilename )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) == nullptr )
    {
        FAIL() << "Could not get the current working directory";
    }

    const std::shared_ptr<CacheAndPersist> persistencyPtr =
        std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
    persistencyPtr->init();
    const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
    AwsIotChannel channel( mConnectivityModule.get(), payloadManager, mMqttClientWrapper, "topic", false );
    std::string filename;
    ASSERT_EQ( channel.sendFile( filename, 0 ), ConnectivityError::WrongInputData );
    channel.invalidateConnection();
}

/** @brief Test sending file over MQTT, file size too big */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTBigFile )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) == nullptr )
    {
        FAIL() << "Could not get the current working directory";
    }

    const std::shared_ptr<CacheAndPersist> persistencyPtr =
        std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
    persistencyPtr->init();
    const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
    AwsIotChannel channel( mConnectivityModule.get(), payloadManager, mMqttClientWrapper, "topic", false );
    std::string filename = "testFile.json";
    ASSERT_EQ( channel.sendFile( filename, 150000 ), ConnectivityError::WrongInputData );
    channel.invalidateConnection();
}

TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendFileOverMQTTNoFile )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) == nullptr )
    {
        FAIL() << "Could not get the current working directory";
    }

    const std::shared_ptr<CacheAndPersist> persistencyPtr =
        std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
    persistencyPtr->init();
    const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );

    AwsIotChannel channel( mConnectivityModule.get(), payloadManager, mMqttClientWrapper, "topic", false );

    std::string filename = "testFile.json";
    ASSERT_EQ( channel.sendFile( filename, 100 ), ConnectivityError::WrongInputData );

    channel.invalidateConnection();
}

TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendFileOverMQTTSdkRAMExceeded )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) == nullptr )
    {
        FAIL() << "Could not get the current working directory";
    }

    auto &memMgr = AwsSDKMemoryManager::getInstance();
    void *alloc1 = memMgr.AllocateMemory( 600000000, alignof( std::size_t ) );
    ASSERT_NE( alloc1, nullptr );
    memMgr.FreeMemory( alloc1 );

    void *alloc2 = memMgr.AllocateMemory( 600000010, alignof( std::size_t ) );
    ASSERT_NE( alloc2, nullptr );
    memMgr.FreeMemory( alloc2 );

    const std::shared_ptr<CacheAndPersist> persistencyPtr =
        std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
    persistencyPtr->init();

    const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );

    AwsIotChannel channel( mConnectivityModule.get(), payloadManager, mMqttClientWrapper, "topic", false );

    // Fake file content
    std::array<std::uint8_t, 2> input = { 0xCA, 0xFE };
    const auto required = input.size() * sizeof( std::uint8_t );
    std::string filename = "testFile.bin";
    {
        void *alloc3 =
            memMgr.AllocateMemory( 50 * AwsSDKMemoryManager::getInstance().getLimit(), alignof( std::size_t ) );
        ASSERT_NE( alloc3, nullptr );
        CollectionSchemeParams collectionSchemeParams;
        collectionSchemeParams.persist = true;
        collectionSchemeParams.compression = false;
        ASSERT_EQ( channel.sendFile( filename, input.size() * sizeof( std::uint8_t ), collectionSchemeParams ),
                   ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc3 );
    }
    {
        constexpr auto offset = alignof( std::max_align_t );
        // check that we will be out of memory even if we allocate less than the max because of the allocator's
        // offset in the below alloc we are leaving space for the input
        auto alloc4 = memMgr.AllocateMemory( AwsSDKMemoryManager::getInstance().getLimit() - ( offset + required ),
                                             alignof( std::size_t ) );
        ASSERT_NE( alloc4, nullptr );
        ASSERT_EQ( channel.sendFile( filename, sizeof( input ) ), ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc4 );
    }

    channel.invalidateConnection();
}

TEST_F( AwsIotConnectivityModuleTestAfterSuccessfulConnection, sendFileOverMQTT )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) == nullptr )
    {
        FAIL() << "Could not get the current working directory";
    }

    int ret = std::system( "rm -rf ./Persistency && mkdir ./Persistency" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );

    const std::shared_ptr<CacheAndPersist> persistencyPtr =
        std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
    persistencyPtr->init();

    std::string testData = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
    const uint8_t *stringData = reinterpret_cast<const uint8_t *>( testData.data() );

    std::string filename = "testFile.bin";
    persistencyPtr->write( stringData, testData.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename );

    const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );

    AwsIotChannel channel( mConnectivityModule.get(), payloadManager, mMqttClientWrapper, "topic", false );

    std::list<Aws::Crt::Mqtt5::OnPublishCompletionHandler> completeHandlers;
    EXPECT_CALL( *mMqttClientWrapperMock, Publish( _, _ ) )
        .Times( 2 )
        .WillRepeatedly(
            Invoke( [&completeHandlers](
                        std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket>,
                        Aws::Crt::Mqtt5::OnPublishCompletionHandler onPublishCompletionCallback ) noexcept -> bool {
                completeHandlers.push_back( std::move( onPublishCompletionCallback ) );
                return true;
            } ) );

    ASSERT_EQ( channel.sendFile( filename, testData.size() ), ConnectivityError::Success );

    // Confirm 1st
    completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
    completeHandlers.pop_front();

    // Test callback return false
    persistencyPtr->write( stringData, testData.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_EQ( channel.sendFile( filename, testData.size() ), ConnectivityError::Success );

    completeHandlers.front().operator()( 0, std::make_shared<PublishResult>() );
    completeHandlers.pop_front();

    channel.invalidateConnection();
}

/** @brief Test sending file over MQTT, no connection */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoConnection )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) == nullptr )
    {
        FAIL() << "Could not get the current working directory";
    }

    const std::shared_ptr<CacheAndPersist> persistencyPtr =
        std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
    persistencyPtr->init();
    const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
    AwsIotChannel channel( mConnectivityModule.get(), payloadManager, mMqttClientWrapper, "topic", false );
    std::string filename = "testFile.json";
    ASSERT_EQ( channel.sendFile( filename, 100 ), ConnectivityError::NoConnection );
    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.persist = true;
    collectionSchemeParams.compression = false;
    ASSERT_EQ( channel.sendFile( filename, 100, collectionSchemeParams ), ConnectivityError::NoConnection );
    channel.invalidateConnection();
}

/** @brief Test the separate thread with exponential backoff that tries to connect until connection succeeds */
TEST_F( AwsIotConnectivityModuleTest, asyncConnect )
{
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
    EXPECT_CALL( *mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
    // Don't automatically invoke the callback, we want to call it manually.
    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );

    ASSERT_TRUE( mConnectivityModule->connect() );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // first attempt should come immediately

    EXPECT_CALL( *mMqttClientWrapperMock, Start() ).WillOnce( Return( true ) );
    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).WillOnce( Return( true ) );
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
    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).WillOnce( Return( true ) );
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

    EXPECT_CALL( *mMqttClientWrapperMock, Stop() ).Times( 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
