// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "AwsIotChannel.h"
#include "AwsIotSdkMock.h"
#include "AwsSDKMemoryManager.h"
#include <array>
#include <cstddef>
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

using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;
using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot::Testing;
using namespace Aws::Crt::Mqtt;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;

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

        ON_CALL( sdkMock, ByteCursorFromCString( _ ) ).WillByDefault( Return( byteCursor ) );

        ON_CALL( sdkMock, ByteBufNewCopy( _, _, _ ) ).WillByDefault( Return( byteBuffer ) );

        ON_CALL( sdkMock, aws_error_debug_str( _ ) ).WillByDefault( Return( "aws_error_debug_str Mock called" ) );

        bootstrap = new Aws::Crt::Io::ClientBootstrap();
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
            .WillByDefault( Invoke( [&con]( const char *, bool, uint16_t, uint32_t ) noexcept -> bool {
                con->OnConnectionCompleted( *con, 0, ReturnCode::AWS_MQTT_CONNECT_ACCEPTED, true );
                return true;
            } ) );

        ON_CALL( *con, Disconnect() ).WillByDefault( Return( true ) );

        return con;
    }

    void
    TearDown() override
    {
        delete ( bootstrap );
    }

    NiceMock<AwsIotSdkMock> sdkMock;
    NiceMock<MqttConnectionMock> conMock;
    NiceMock<MqttClientMock> clientMock;
    NiceMock<MqttClientConnectionConfigMock> confMock;
    NiceMock<MqttClientConnectionConfigBuilderMock> confBuilder;
    NiceMock<ClientBootstrapMock> clientBootstrap;
    NiceMock<EventLoopGroupMock> eventLoopMock;
    Aws::Crt::ByteBuf byteBuffer;
    uint8_t byte;
    Aws::Crt::ByteCursor byteCursor = { 1, &byte };
    Aws::Crt::Io::ClientBootstrap *bootstrap;
};

/** @brief  Test attempting to disconnect when connection has already failed */
TEST_F( AwsIotConnectivityModuleTest, disconnectAfterFailedConnect )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "", "", "", "", "", bootstrap ) );
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

    ASSERT_TRUE( m->connect( "key", "cert", "rootca", endpoint, "clientIdTest", bootstrap ) );

    con->OnDisconnect( *con );

    m->disconnect();

    // no modification should be made to endpoint
    ASSERT_EQ( endpoint, requestedEndpoint );
}

/** @brief Test trying to connect, where creation of the bootstrap fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientBootstrapCreation )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", nullptr ) );
}

/** @brief Test trying to connect, where creation of the client fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientCreation )
{
    auto con = setupValidConnection();
    EXPECT_CALL( clientMock, operatorBool() ).Times( AtLeast( 1 ) ).WillRepeatedly( Return( false ) );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_FALSE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );
}

/** @brief Test opening a connection, then interrupting it and resuming it */
TEST_F( AwsIotConnectivityModuleTest, connectionInterrupted )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );

    con->OnConnectionInterrupted( *con, 10 );
    con->OnConnectionResumed( *con, ReturnCode::AWS_MQTT_CONNECT_ACCEPTED, true );

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
        con->OnConnectionCompleted( *con, 0, ReturnCode::AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
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

    ASSERT_FALSE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );
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

    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );
    c.setTopic( "topic" );
    EXPECT_CALL( *con, Subscribe( _, _, _, _ ) )
        .Times( 1 )
        .WillRepeatedly( Invoke( [&con]( const char *,
                                         aws_mqtt_qos,
                                         MqttConnection::OnMessageReceivedHandler &&,
                                         MqttConnection::OnSubAckHandler &&onSubAck ) -> bool {
            onSubAck( *con, 10, "topic", aws_mqtt_qos::AWS_MQTT_QOS_AT_MOST_ONCE, 0 );
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
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test sending without a connection, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutConnection )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    std::uint8_t input[] = { 0xca, 0xfe };
    c.setTopic( "topic" );
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::NoConnection );
    c.invalidateConnection();
}

/** @brief Test passing a null pointer, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWrongInput )
{
    auto con = setupValidConnection();
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );
    c.setTopic( "topic" );
    ASSERT_EQ( c.sendBuffer( nullptr, 10 ), ConnectivityError::WrongInputData );
    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test sending a message larger then the maximum send size, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendTooBig )
{
    auto con = setupValidConnection();
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    AwsIotChannel c( m.get(), nullptr );
    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );
    c.setTopic( "topic" );
    std::vector<uint8_t> a;
    a.resize( c.getMaxSendSize() + 1U );
    ASSERT_EQ( c.sendBuffer( a.data(), a.size() ), ConnectivityError::WrongInputData );
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
    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );
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
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 1st (success as packetId is 1---v):
    completeHandlers.front().operator()( *con, 1, 0 );
    completeHandlers.pop_front();

    // Queue another:
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 2nd (success as packetId is 2---v):
    completeHandlers.front().operator()( *con, 2, 0 );
    completeHandlers.pop_front();
    // Confirm 3rd (failure as packetId is 0---v) (Not a test case failure, but a stimulated failure for code coverage)
    completeHandlers.front().operator()( *con, 0, 0 );
    completeHandlers.pop_front();

    ASSERT_EQ( c.getPayloadCountSent(), 2 );

    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test SDK exceeds RAM and Channel stops sending */
TEST_F( AwsIotConnectivityModuleTest, sdkRAMExceeded )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();
    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap ) );

    auto &memMgr = AwsSDKMemoryManager::getInstance();
    void *alloc1 = memMgr.AllocateMemory( 600000000, alignof( std::size_t ) );
    ASSERT_NE( alloc1, nullptr );
    memMgr.FreeMemory( alloc1 );

    void *alloc2 = memMgr.AllocateMemory( 600000010, alignof( std::size_t ) );
    ASSERT_NE( alloc2, nullptr );
    memMgr.FreeMemory( alloc2 );

    AwsIotChannel c( m.get(), nullptr );
    c.setTopic( "topic" );
    std::array<std::uint8_t, 2> input = { 0xCA, 0xFE };
    const auto required = input.size() * sizeof( std::uint8_t );
    {
        void *alloc3 =
            memMgr.AllocateMemory( 50 * AwsIotChannel::MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES, alignof( std::size_t ) );
        ASSERT_NE( alloc3, nullptr );

        ASSERT_EQ( c.sendBuffer( input.data(), input.size() * sizeof( std::uint8_t ) ),
                   ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc3 );
    }
    {
        constexpr auto offset = alignof( std::max_align_t );
        // check that we will be out of memory even if we allocate less than the max because of the allocator's offset
        // in the below alloc we are leaving space for the input
        auto alloc4 = memMgr.AllocateMemory( AwsIotChannel::MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES - ( offset + required ),
                                             alignof( std::size_t ) );
        ASSERT_NE( alloc4, nullptr );
        ASSERT_EQ( c.sendBuffer( input.data(), sizeof( input ) ), ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc4 );

        // check that allocation and hence send succeed when there is just enough memory
        // here we subtract the offset twice - once for MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES and once for the input
        auto alloc5 = memMgr.AllocateMemory(
            AwsIotChannel::MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES - ( ( 2 * offset ) + required ), alignof( std::size_t ) );
        ASSERT_NE( alloc5, nullptr );

        std::list<MqttConnection::OnOperationCompleteHandler> completeHandlers;
        EXPECT_CALL( *con, Publish( _, _, _, _, _ ) )
            .Times( AnyNumber() )
            .WillRepeatedly( Invoke(
                [&completeHandlers]( const char *,
                                     aws_mqtt_qos,
                                     bool,
                                     const struct aws_byte_buf &,
                                     MqttConnection::OnOperationCompleteHandler &&onOpComplete ) noexcept -> bool {
                    completeHandlers.push_back( std::move( onOpComplete ) );
                    return true;
                } ) );

        ASSERT_EQ( c.sendBuffer( input.data(), sizeof( input ) ), ConnectivityError::Success );
        memMgr.FreeMemory( alloc5 );

        // // Confirm 1st (success as packetId is 1---v):
        completeHandlers.front().operator()( *con, 1, 0 );
        completeHandlers.pop_front();
    }

    con->OnDisconnect( *con );
    c.invalidateConnection();
}

/** @brief Test the separate thread with exponential backoff that tries to connect until connection succeeds */
TEST_F( AwsIotConnectivityModuleTest, asyncConnect )
{
    auto con = setupValidConnection();

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>();

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );

    ASSERT_TRUE( m->connect( "key", "cert", "rootca", "endpoint", "clientIdTest", bootstrap, true ) );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // first attempt should come immediately

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    con->OnConnectionCompleted( *con, 0, ReturnCode::AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    con->OnDisconnect( *con );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) ); // minimum wait time 1 second

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    con->OnConnectionCompleted( *con, 0, ReturnCode::AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    con->OnDisconnect( *con );
    std::this_thread::sleep_for( std::chrono::milliseconds( 2100 ) ); // exponential backoff now 2 seconds

    EXPECT_CALL( *con, Connect( _, _, _, _ ) ).Times( 0 );
    con->OnConnectionCompleted( *con, 0, ReturnCode::AWS_MQTT_CONNECT_ACCEPTED, true );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    con->OnDisconnect( *con );
}
