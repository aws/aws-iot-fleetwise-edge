// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "businterfaces/ISOTPOverCANSenderReceiver.h"
#include "ClockHandler.h"
#include <cstring>
#include <iostream>
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// ISO TP maximum PDU size is 4095, additional bytes are needed
// for the Linux Networking stack internals.
#define MAX_PDU_SIZE 5000

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

bool
ISOTPOverCANSenderReceiver::init( const ISOTPOverCANSenderReceiverOptions &senderReceiverOptions )
{
    mTimer.reset();
    mSenderReceiverOptions = senderReceiverOptions;
    std::stringstream streamRxId;
    streamRxId << std::hex << mSenderReceiverOptions.mDestinationCANId;
    mStreamRxID = streamRxId.str();
    return true;
}

bool
ISOTPOverCANSenderReceiver::connect()
{
    // Socket CAN parameters
    struct sockaddr_can interfaceAddress = {};
    struct can_isotp_options optionalFlags = {};
    struct can_isotp_fc_options frameControlFlags = {};

    // Set the source and the destination
    interfaceAddress.can_addr.tp.tx_id = mSenderReceiverOptions.mSourceCANId;
    interfaceAddress.can_addr.tp.rx_id = mSenderReceiverOptions.mDestinationCANId;
    // Both source and destination are extended CANIDs
    if ( mSenderReceiverOptions.mIsExtendedId )
    {
        interfaceAddress.can_addr.tp.tx_id |= CAN_EFF_FLAG;
        interfaceAddress.can_addr.tp.rx_id |= CAN_EFF_FLAG;
    }
    optionalFlags.flags |= CAN_ISOTP_TX_PADDING;
    // Set the block size
    frameControlFlags.bs = mSenderReceiverOptions.mBlockSize & 0xFF;
    // Set the Separation time
    frameControlFlags.stmin = mSenderReceiverOptions.mFrameSeparationTimeMs & 0xFF;
    // Number of wait frames. Set to zero in FWE case as we can wait on reception.
    frameControlFlags.wftmax = 0x0;

    // Open a Socket
    mSocket = socket( PF_CAN, SOCK_DGRAM, CAN_ISOTP );
    if ( mSocket < 0 )
    {
        mLogger.error( "ISOTPOverCANSenderReceiver::connect",
                       " Failed to create the ISOTP rx id " + mStreamRxID +
                           " to IF:" + mSenderReceiverOptions.mSocketCanIFName );
        return false;
    }

    // Set the Frame Control Flags
    int retOptFlag = setsockopt( mSocket, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &optionalFlags, sizeof( optionalFlags ) );

    int retFrameCtrFlag =
        setsockopt( mSocket, SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, &frameControlFlags, sizeof( frameControlFlags ) );

    if ( retOptFlag < 0 || retFrameCtrFlag < 0 )
    {
        mLogger.error( "ISOTPOverCANSenderReceiver::connect", " Failed to set ISO-TP socket option flags" );
        return false;
    }
    // CAN PF and Interface Index
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex =
        static_cast<int>( if_nametoindex( mSenderReceiverOptions.mSocketCanIFName.c_str() ) );

    // Bind the socket
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    if ( bind( mSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        mLogger.error( "ISOTPOverCANSenderReceiver::connect",
                       " Failed to bind the ISOTP rx id " + mStreamRxID +
                           " to IF:" + mSenderReceiverOptions.mSocketCanIFName );
        close( mSocket );
        return false;
    }
    mLogger.trace( "ISOTPOverCANSenderReceiver::connect",
                   " ISOTP rx id " + mStreamRxID + " connected to IF:" + mSenderReceiverOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANSenderReceiver::disconnect()
{
    if ( close( mSocket ) < 0 )
    {
        mLogger.error( "ISOTPOverCANSenderReceiver::connect",
                       " Failed to disconnect the ISOTP rx id " + mStreamRxID +
                           " from IF:" + mSenderReceiverOptions.mSocketCanIFName );
        return false;
    }
    mLogger.trace( "ISOTPOverCANSenderReceiver::disconnect",
                   " ISOTP rx id " + mStreamRxID + " disconnected from IF:" + mSenderReceiverOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANSenderReceiver::isAlive() const
{
    int error = 0;
    socklen_t len = sizeof( error );
    // Get the error status of the socket
    int retSockOpt = getsockopt( mSocket, SOL_SOCKET, SO_ERROR, &error, &len );
    return ( retSockOpt == 0 && error == 0 );
}

bool
ISOTPOverCANSenderReceiver::receivePDU( std::vector<uint8_t> &pduData )
{
    if ( mSenderReceiverOptions.mP2TimeoutMs > P2_TIMEOUT_INFINITE )
    {
        struct pollfd pfd = { mSocket, POLLIN, 0 };
        int res = poll( &pfd, 1U, static_cast<int>( mSenderReceiverOptions.mP2TimeoutMs ) );
        if ( res <= 0 )
        {
            mLogger.warn( "ISOTPOverCANSenderReceiver::receivePDU",
                          " Failed to read PDU from socket:" + mStreamRxID + " with error code " +
                              std::to_string( res ) );
            // Error (<0) or timeout (==0):
            return false;
        }
    }
    // To pass on the vector to read, we need to reserve some bytes.
    pduData.resize( MAX_PDU_SIZE );
    // coverity[check_return : SUPPRESS]
    int bytesRead = static_cast<int>( read( mSocket, pduData.data(), MAX_PDU_SIZE ) );
    // Remove the unnecessary bytes from the PDU container.
    if ( bytesRead > 0 )
    {
        pduData.resize( static_cast<size_t>( bytesRead ) );
    }
    else
    {
        pduData.resize( 0 );
    }
    mLogger.traceBytesInVector( "ISOTPOverCANSenderReceiver::receivePDU",
                                "Socket:" + mStreamRxID + " received a PDU of size " + std::to_string( bytesRead ),
                                pduData );

    return bytesRead > 0;
}

bool
ISOTPOverCANSenderReceiver::sendPDU( const std::vector<uint8_t> &pduData )
{
    auto socket = mSenderReceiverOptions.mBroadcastSocket < 0 ? mSocket : mSenderReceiverOptions.mBroadcastSocket;
    int bytesWritten = static_cast<int>( write( socket, pduData.data(), pduData.size() ) );
    mLogger.traceBytesInVector( "ISOTPOverCANSenderReceiver::sendPDU",
                                "Socket:" + mStreamRxID + " sent a PDU of size " + std::to_string( bytesWritten ),
                                pduData );
    return ( bytesWritten > 0 && bytesWritten == static_cast<int>( pduData.size() ) );
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
