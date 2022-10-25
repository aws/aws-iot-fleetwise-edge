// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "businterfaces/ISOTPOverCANSender.h"
#include "ClockHandler.h"
#include <cstring>
#include <iostream>
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

bool
ISOTPOverCANSender::init( const ISOTPOverCANSenderOptions &senderOptions )
{
    mTimer.reset();
    mSenderOptions = senderOptions;
    return true;
}

bool
ISOTPOverCANSender::connect()
{
    // Socket CAN parameters
    struct sockaddr_can interfaceAddress = {};
    struct can_isotp_options optionalFlags = {};

    // Set the source and the destination
    interfaceAddress.can_addr.tp.tx_id = mSenderOptions.mSourceCANId;
    interfaceAddress.can_addr.tp.rx_id = mSenderOptions.mDestinationCANId;
    // Both source and destination are extended CANIDs
    if ( mSenderOptions.mIsExtendedId )
    {
        interfaceAddress.can_addr.tp.tx_id |= CAN_EFF_FLAG;
        interfaceAddress.can_addr.tp.rx_id |= CAN_EFF_FLAG;
    }

    // Open a Socket in default blocking mode
    // We don't need to change this on the write as we know there will
    // be something in the buffer anyway.
    mSocket = socket( PF_CAN, SOCK_DGRAM /*| SOCK_NONBLOCK*/, CAN_ISOTP );
    if ( mSocket < 0 )
    {
        mLogger.error( "ISOTPOverCANSender::connect",
                       " Failed to create the ISOTP Socket to IF:" + mSenderOptions.mSocketCanIFName );
        return false;
    }

    // Set the Frame Control Flags
    int retOptFlag = setsockopt( mSocket, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &optionalFlags, sizeof( optionalFlags ) );
    if ( retOptFlag < 0 )
    {
        mLogger.error( "ISOTPOverCANSender::connect", " Failed to set ISO-TP socket option flags" );
        return false;
    }
    // CAN PF and Interface Index
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = static_cast<int>( if_nametoindex( mSenderOptions.mSocketCanIFName.c_str() ) );

    // Bind the socket
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    if ( bind( mSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        mLogger.error( "ISOTPOverCANSender::connect",
                       " Failed to bind the ISOTP Socket to IF:" + mSenderOptions.mSocketCanIFName );
        close( mSocket );
        return false;
    }
    mLogger.trace( "ISOTPOverCANSender::connect", " ISOTP Socket connected to IF:" + mSenderOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANSender::disconnect()
{
    if ( close( mSocket ) < 0 )
    {
        mLogger.error( "ISOTPOverCANSender::connect",
                       " Failed to disconnect the ISOTP Socket from IF:" + mSenderOptions.mSocketCanIFName );
        return false;
    }
    mLogger.trace( "ISOTPOverCANSender::disconnect",
                   " ISOTP Socket disconnected from IF:" + mSenderOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANSender::isAlive() const
{
    int error = 0;
    socklen_t len = sizeof( error );
    // Get the error status of the socket
    int retSockOpt = getsockopt( mSocket, SOL_SOCKET, SO_ERROR, &error, &len );
    return ( retSockOpt == 0 && error == 0 );
}

bool
ISOTPOverCANSender::sendPDU( const std::vector<uint8_t> &pduData )
{
    int bytesWritten = static_cast<int>( write( mSocket, pduData.data(), pduData.size() ) );
    mLogger.trace( "ISOTPOverCANSender::sendPDU", " sent a PDU of size:" + std::to_string( bytesWritten ) );
    return ( bytesWritten > 0 && bytesWritten == static_cast<int>( pduData.size() ) );
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
