/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "AwsIotConnectivityModule.h"
#include "AwsIotChannel.h"
#include "AwsIotSdkMock.h"
#include <gtest/gtest.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <list>
#include <net/if.h>
#include <snappy.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define UNUSED( x ) (void)( x )

using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;
using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot::Testing;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;

Aws::Crt::Allocator *Aws::Crt::ApiHandle::mAllocator = nullptr;

class AwsIotConnectivityModuleTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {

        setSdkMock( &sdkMock );
        setConMock( &conMock );

        setClientMock( &clientMock );

        setConfMock( &confMock );

        setConfBuilderMock( &confBuilder );

        setClientBootstrapMock( &clientBootstrap );

        setEventLoopMock( &eventLoopMock );

        ON_CALL( sdkMock, ErrorDebugString( _ ) ).WillByDefault( Return( "ErrorDebugString Mock called" ) );

        ON_CALL( sdkMock, ByteBufNewCopy( _, _, _ ) ).WillByDefault( Return( byteBuffer ) );

        ON_CALL( sdkMock, aws_error_debug_str( _ ) ).WillByDefault( Return( "aws_error_debug_str Mock called" ) );
    }

    std::shared_ptr<NiceMock<MqttConnectionMock>>
    setupValidConnection()
    {
        ON_CALL( eventLoopMock, operatorBool() ).WillByDefault( Return( true ) );
        ON_CALL( clientBootstrap, operatorBool() ).WillByDefault( Return( true ) );

        ON_CALL( confMock, operatorBool() ).WillByDefault( Return( true ) );
        ON_CALL( clientMock, operatorBool() ).WillByDefault( Return( true ) );

        std::shared_ptr<NiceMock<MqttConnectionMock>> con = std::make_shared<NiceMock<MqttConnectionMock>>();
        ON_CALL( clientMock, NewConnection( _ ) ).WillByDefault( Return( con ) );
        ON_CALL( *con, SetOnMessageHandler( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( *con, Connect( _, _, _, _ ) )
            .WillByDefault( Invoke( [&con]( const char *clientId,
                                            bool cleanSession,
                                            uint16_t keepAliveTimeSecs,
                                            uint32_t pingTimeoutMs ) noexcept -> bool {
                UNUSED( clientId );
                UNUSED( cleanSession );
                UNUSED( keepAliveTimeSecs );
                UNUSED( pingTimeoutMs );
                con->OnConnectionCompleted( *con, 0, AWS_MQTT_CONNECT_ACCEPTED, true );
                return true;
            } ) );

        ON_CALL( *con, Disconnect() ).WillByDefault( Return( true ) );

        return con;
    }

    void
    TearDown() override
    {
    }

    NiceMock<AwsIotSdkMock> sdkMock;
    NiceMock<MqttConnectionMock> conMock;
    NiceMock<MqttClientMock> clientMock;
    NiceMock<MqttClientConnectionConfigMock> confMock;
    NiceMock<MqttClientConnectionConfigBuilderMock> confBuilder;
    NiceMock<ClientBootstrapMock> clientBootstrap;
    NiceMock<EventLoopGroupMock> eventLoopMock;
    Aws::Crt::ByteBuf byteBuffer;
};

/** @brief  Test attempting to disconnect when connection has already failed */
TEST_F( AwsIotConnectivityModuleTest, disconnectAfterFailedConnect )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "", "", "", "" ) );
    // disconnect must only disconnect when connection is available so this should not seg fault
    m->disconnect();
}

/** @brief Test successful connection */
TEST_F( AwsIotConnectivityModuleTest, connectSuccessfull )
{
    std::string endpoint( "endpoint" );
    auto con = setupValidConnection();

    std::string requestedEndpoint;
    EXPECT_CALL( confBuilder, WithEndpoint( _ ) ).Times( 1 ).WillOnce( SaveArg<0>( &requestedEndpoint ) );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();

    ASSERT_TRUE( m->connect( "key", "cert", endpoint, "clientIdTest" ) );

    con->OnDisconnect( *con );

    m->disconnect();

    // no modification should be made to endpoint
    ASSERT_EQ( endpoint, requestedEndpoint );
}

/** @brief Test trying to connect, where creation of the event loop fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnEventLoopCreation )
{
    auto con = setupValidConnection();
    EXPECT_CALL( eventLoopMock, operatorBool() ).Times( AtLeast( 1 ) ).WillRepeatedly( Return( false ) );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
}

/** @brief Test trying to connect, where creation of the bootstrap fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientBootstrapCreation )
{
    auto con = setupValidConnection();
    EXPECT_CALL( clientBootstrap, operatorBool() ).Times( AtLeast( 1 ) ).WillRepeatedly( Return( false ) );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
}

/** @brief Test trying to connect, where creation of the client fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientCreation )
{
    auto con = setupValidConnection();
    EXPECT_CALL( clientMock, operatorBool() ).Times( AtLeast( 1 ) ).WillRepeatedly( Return( false ) );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
}

/** @brief Test opening a connection, then interrupting it and resuming it */
TEST_F( AwsIotConnectivityModuleTest, connectionInterrupted )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );

    con->OnConnectionInterrupted( *con, 10 );
    con->OnConnectionResumed( *con, AWS_MQTT_CONNECT_ACCEPTED, true );

    con->OnDisconnect( *con );
}

/** @brief Test connecting when it fails after a delay */
TEST_F( AwsIotConnectivityModuleTest, connectFailsServerUnavailableWithDelay )
{
    auto con = setupValidConnection();
    std::atomic<bool> killAllThread( false );
    std::promise<void> completed;
    std::thread completeThread( [con, &killAllThread, &completed]() {
        while ( con->OnConnectionCompleted == nullptr && !killAllThread )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        con->OnConnectionCompleted( *con, 0, AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
        completed.set_value();
    } );

    std::thread disconnectThread( [con, &killAllThread, &completed]() {
        while ( con->OnDisconnect == nullptr && !killAllThread )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        completed.get_future().wait();
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        con->OnDisconnect( *con );
    } );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    // We want to see exactly one call to disconnect
    EXPECT_CALL( *con, Disconnect() ).Times( 1 ).WillRepeatedly( Return( true ) );

    ASSERT_FALSE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    killAllThread = true;
    completeThread.join();
    disconnectThread.join();
}

/** @brief Test subscribing without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutTopic )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    ASSERT_EQ( c.subscribe(), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test subscribing without being connected, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutBeingConnected )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    c.setTopic( "topic" );
    ASSERT_EQ( c.subscribe(), ConnectivityError::NoConnection );
    c.invalidateConnection();
}

/** @brief Test successful subscription */
TEST_F( AwsIotConnectivityModuleTest, subscribeSuccessfully )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );

    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
    c.setTopic( "topic" );
    EXPECT_CALL( *con, Subscribe( _, _, _, _ ) )
        .Times( 1 )
        .WillRepeatedly( Invoke( [&con]( const char *,
                                         aws_mqtt_qos,
                                         MqttConnection::OnMessageReceivedHandler &&,
                                         MqttConnection::OnSubAckHandler &&onSubAck ) -> bool {
            onSubAck( *con, 10, "topic", AWS_MQTT_QOS_AT_MOST_ONCE, 0 );
            return true;
        } ) );
    ASSERT_EQ( c.subscribe(), ConnectivityError::Success );

    EXPECT_CALL( *con, Unsubscribe( _, _ ) )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( Invoke(
            [&con]( const char *, MqttConnection::OnOperationCompleteHandler &&onOpComplete ) noexcept -> uint16_t {
                onOpComplete( *con, 0, 0 );
                return 0;
            } ) );
    c.unsubscribe();
    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutTopic )
{
    auto con = setupValidConnection();
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    std::uint8_t input[] = { 0xca, 0xfe };
    ASSERT_EQ( c.send( input, sizeof( input ) ), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test sending without a connection, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutConnection )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    std::uint8_t input[] = { 0xca, 0xfe };
    c.setTopic( "topic" );
    ASSERT_EQ( c.send( input, sizeof( input ) ), ConnectivityError::NoConnection );
    c.invalidateConnection();
}

/** @brief Test passing a null pointer, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWrongInput )
{
    auto con = setupValidConnection();
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
    c.setTopic( "topic" );
    ASSERT_EQ( c.send( nullptr, 10 ), ConnectivityError::WrongInputData );
    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test sending a message larger then the maximum send size, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendTooBig )
{
    auto con = setupValidConnection();
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
    c.setTopic( "topic" );
    std::vector<uint8_t> a;
    a.resize( c.getMaxSendSize() + 1U );
    ASSERT_EQ( c.send( a.data(), a.size() ), ConnectivityError::WrongInputData );
    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test sending multiple messages. The API supports queuing of messages, so send more than one
 * message, allow one to be sent, queue another and then send the rest. Also indicate one of
 * messages as failed to send to check that path. */
TEST_F( AwsIotConnectivityModuleTest, sendMultiple )
{
    auto con = setupValidConnection();
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );
    std::uint8_t input[] = { 0xca, 0xfe };
    c.setTopic( "topic" );
    std::list<MqttConnection::OnOperationCompleteHandler> completeHandlers;
    EXPECT_CALL( *con, Publish( _, _, _, _, _ ) )
        .Times( 3 )
        .WillRepeatedly(
            Invoke( [&completeHandlers]( const char *,
                                         aws_mqtt_qos,
                                         bool,
                                         const struct aws_byte_buf &,
                                         MqttConnection::OnOperationCompleteHandler &&onOpComplete ) noexcept -> bool {
                completeHandlers.push_back( std::move( onOpComplete ) );
                return true;
            } ) );

    // Queue 2 packets
    ASSERT_EQ( c.send( input, sizeof( input ) ), ConnectivityError::Success );
    ASSERT_EQ( c.send( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 1st (success as packetId is 1---v):
    completeHandlers.front().operator()( *con, 1, 0 );
    completeHandlers.pop_front();

    // Queue another:
    ASSERT_EQ( c.send( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 2nd (success as packetId is 2---v):
    completeHandlers.front().operator()( *con, 2, 0 );
    completeHandlers.pop_front();
    // Confirm 3rd (failure as packetId is 0---v) (Not a test case failure, but a stimulated failure for code coverage)
    completeHandlers.front().operator()( *con, 0, 0 );
    completeHandlers.pop_front();

    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test SDK exceeds RAM and Channel stops sending */
TEST_F( AwsIotConnectivityModuleTest, sdkRAMExceeded )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest" ) );

    auto allocator = Aws::Crt::ApiHandle::getLatestAllocator();

    ASSERT_NE( allocator, nullptr );

    void *alloc1 = allocator->mem_acquire( allocator, 600000000 );
    ASSERT_NE( alloc1, nullptr );

    void *alloc2 = allocator->mem_realloc( allocator, alloc1, 600000000, 600000010 );
    ASSERT_NE( alloc2, nullptr );

    void *alloc3 = allocator->mem_realloc( allocator, alloc2, 600000010, 500000000 );
    ASSERT_NE( alloc3, nullptr );

    AwsIotChannel c( m.get(), nullptr );
    c.setTopic( "topic" );

    std::uint8_t input[] = { 0xCA, 0xFE };
    ASSERT_EQ( c.send( input, sizeof( input ) ), ConnectivityError::QuotaReached );

    allocator->mem_release( allocator, alloc3 );

    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test the separate thread with exponential backoff that tries to connect until connection succeeds */
TEST_F( AwsIotConnectivityModuleTest, asyncConnect )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );

    ASSERT_TRUE( m->connect( "key", "cert", "endpoint", "clientIdTest", true ) );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // first attempt should come immediately

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    con->OnConnectionCompleted( *con, 0, AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    con->OnDisconnect( *con );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) ); // minimum wait time 1 second

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    con->OnConnectionCompleted( *con, 0, AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    con->OnDisconnect( *con );
    std::this_thread::sleep_for( std::chrono::milliseconds( 2100 ) ); // exponential backoff now 2 seconds

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 0 );
    con->OnConnectionCompleted( *con, 0, AWS_MQTT_CONNECT_ACCEPTED, true );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    con->OnDisconnect( *con );
}