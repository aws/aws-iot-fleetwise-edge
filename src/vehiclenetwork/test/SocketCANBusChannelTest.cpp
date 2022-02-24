
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

#include "businterfaces/SocketCANBusChannel.h"
#include <functional>
#include <gtest/gtest.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>

using namespace Aws::IoTFleetWise::VehicleNetwork;

static void
cleanUp( int socketFD )
{
    close( socketFD );
}

static int
setup()
{
    // Setup a socket
    std::string socketCANIFName( "vcan0" );
    struct sockaddr_can interfaceAddress;
    struct ifreq interfaceRequest;

    int type = SOCK_RAW | SOCK_NONBLOCK;
    int socketFD = socket( PF_CAN, type, CAN_RAW );
    if ( socketFD < 0 )
    {
        return -1;
    }

    if ( socketCANIFName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        cleanUp( socketFD );
        return -1;
    }
    (void)strncpy( interfaceRequest.ifr_name, socketCANIFName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );

    if ( ioctl( socketFD, SIOCGIFINDEX, &interfaceRequest ) )
    {
        cleanUp( socketFD );
        return -1;
    }

    memset( &interfaceAddress, 0, sizeof( interfaceAddress ) );
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    if ( bind( socketFD, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        cleanUp( socketFD );
        return -1;
    }

    return socketFD;
}

class localChannelEventListener : public NetworkChannelBridgeListener
{
public:
    localChannelEventListener()
        : gotConnectCallback( false )
        , gotDisConnectCallback( false )
    {
    }

    inline void
    onNetworkChannelConnected( const NetworkChannelID &Id )
    {
        static_cast<void>( Id ); // Ignore parameter
        gotConnectCallback = true;
    }

    inline void
    onNetworkChannelDisconnected( const NetworkChannelID &Id )
    {
        static_cast<void>( Id ); // Ignore parameter
        gotDisConnectCallback = true;
    }

    bool gotConnectCallback;
    bool gotDisConnectCallback;
};

static void
sendTestMessage( int socketFD )
{
    struct can_frame frame = {};
    frame.can_id = 0x123;
    frame.can_dlc = 4;
    for ( uint8_t i = 0; i < 3; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( socketFD, &frame, sizeof( struct can_frame ) );
    ASSERT_EQ( bytesWritten, sizeof( struct can_frame ) );
}

class Finally
{
public:
    Finally( std::function<void()> func )
        : mFunc( func ){};
    ~Finally()
    {
        mFunc();
    }

private:
    std::function<void()> mFunc;
};

TEST( SocketCANBusChannelTest, testAquireDataFromNetwork_LinuxCANDep )
{
    localChannelEventListener listener;
    int socketFD = setup();
    ASSERT_TRUE( socketFD != -1 );
    Finally finally( [=] { cleanUp( socketFD ); } );

    static_cast<void>( socketFD >= 0 );
    SocketCANBusChannel channel( "vcan0", true );
    ASSERT_TRUE( channel.init( 1, 1000 ) );
    ASSERT_TRUE( channel.subscribeListener( &listener ) );

    ASSERT_TRUE( channel.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( channel.isAlive() );
    // Set the Channel in an active acquire state
    channel.resumeDataAcquisition();
    ASSERT_EQ( channel.getChannelID(), 1 );
    ASSERT_EQ( channel.getChannelIfName(), "vcan0" );
    ASSERT_EQ( channel.getChannelProtocol(), NetworkChannelProtocol::RAW_SOCKET );
    ASSERT_EQ( channel.getChannelType(), NetworkChannelType::CAN_CHANNEL );
    ASSERT_EQ( channel.getChannelType(), NetworkChannelType::CAN_CHANNEL );
    sendTestMessage( socketFD );
    // Sleep for sometime on this thread to allow the other thread to finish
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    CANRawMessage msg;
    ASSERT_TRUE( channel.getBuffer()->pop( msg ) );
    ASSERT_TRUE( channel.disconnect() );
    ASSERT_TRUE( channel.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST( SocketCANBusChannelTest, testDoNotAcquireDataFromNetwork_LinuxCANDep )
{
    localChannelEventListener listener;
    int socketFD = setup();
    ASSERT_TRUE( socketFD != -1 );
    Finally finally( [=] { cleanUp( socketFD ); } );
    static_cast<void>( socketFD >= 0 );
    SocketCANBusChannel channel( "vcan0", true );
    ASSERT_TRUE( channel.init( 1, 1000 ) );
    ASSERT_TRUE( channel.subscribeListener( &listener ) );
    ASSERT_TRUE( channel.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( channel.isAlive() );
    // The channel is not acquiring data from the network by default
    // We should test that although data is available in the socket,
    // the channel buffer must be empty
    ASSERT_EQ( channel.getChannelID(), 2 );
    ASSERT_EQ( channel.getChannelIfName(), "vcan0" );
    ASSERT_EQ( channel.getChannelProtocol(), NetworkChannelProtocol::RAW_SOCKET );
    ASSERT_EQ( channel.getChannelType(), NetworkChannelType::CAN_CHANNEL );

    sendTestMessage( socketFD );
    // Sleep for sometime on this thread to allow the other thread to finish
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    CANRawMessage msg;
    // No messages should be in the buffer
    ASSERT_FALSE( channel.getBuffer()->pop( msg ) );
    ASSERT_TRUE( channel.disconnect() ); // Here the frame will be read from the socket
    ASSERT_TRUE( channel.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}

TEST( SocketCANBusChannelTest, testNetworkDataAquisitionStateChange_LinuxCANDep )
{
    // In this test, we want to start the channel with the default settings i.e. sleep mode,
    // then activate data acquisition and check that the channel buffer effectively has a message,
    // then interrupt the consumption and make sure that the channel is in sleep mode.
    localChannelEventListener listener;
    int socketFD = setup();
    ASSERT_TRUE( socketFD != -1 );
    Finally finally( [=] { cleanUp( socketFD ); } );
    static_cast<void>( socketFD >= 0 );
    SocketCANBusChannel channel( "vcan0", true );
    ASSERT_TRUE( channel.init( 1, 1000 ) );
    ASSERT_TRUE( channel.subscribeListener( &listener ) );
    ASSERT_TRUE( channel.connect() );
    ASSERT_TRUE( listener.gotConnectCallback );
    ASSERT_TRUE( channel.isAlive() );
    // The channel is not acquiring data from the network by default
    // We should test that although data is available in the socket,
    // the channel buffer must be empty
    ASSERT_EQ( channel.getChannelID(), 3 );
    ASSERT_EQ( channel.getChannelIfName(), "vcan0" );
    ASSERT_EQ( channel.getChannelProtocol(), NetworkChannelProtocol::RAW_SOCKET );
    ASSERT_EQ( channel.getChannelType(), NetworkChannelType::CAN_CHANNEL );
    ASSERT_EQ( channel.getChannelType(), NetworkChannelType::CAN_CHANNEL );
    // Send a message on the bus.
    sendTestMessage( socketFD );
    // Sleep for sometime on this thread to allow the other thread to finish
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    CANRawMessage msg;
    // No messages should be in the buffer
    ASSERT_FALSE( channel.getBuffer()->pop( msg ) );

    // Activate consumption on the bus and make sure the channel buffer has items.
    channel.resumeDataAcquisition();
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    // Make sure old messages in kernel queue are ignored
    ASSERT_FALSE( channel.getBuffer()->pop( msg ) );
    // Send a message on the bus.
    sendTestMessage( socketFD );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    // 1 message should be in the buffer as the channel is active.
    ASSERT_TRUE( channel.getBuffer()->pop( msg ) );

    // Interrupt data acquisition and make sure that the channel now does not consume data
    // anymore.
    channel.suspendDataAcquisition();
    // Send a message on the bus.
    sendTestMessage( socketFD );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    // No messages should be in the buffer
    ASSERT_FALSE( channel.getBuffer()->pop( msg ) );

    ASSERT_TRUE( channel.disconnect() );
    ASSERT_TRUE( channel.unSubscribeListener( &listener ) );
    ASSERT_TRUE( listener.gotDisConnectCallback );
}
