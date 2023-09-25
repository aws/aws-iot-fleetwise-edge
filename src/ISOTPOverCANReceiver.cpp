// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ISOTPOverCANReceiver.h"
#include "LoggingModule.h"
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// ISO TP maximum PDU size is 4095, additional bytes are needed
// for the Linux Networking stack internals.
#define MAX_PDU_SIZE 5000

namespace Aws
{
namespace IoTFleetWise
{

bool
ISOTPOverCANReceiver::init( const ISOTPOverCANReceiverOptions &receiverOptions )
{
    mTimer.reset();
    mReceiverOptions = receiverOptions;
    return true;
}

bool
ISOTPOverCANReceiver::connect()
{
    // Socket CAN parameters
    struct sockaddr_can interfaceAddress = {};
    struct can_isotp_options optionalFlags = {};
    struct can_isotp_fc_options frameControlFlags = {};

    // Set the source and the destination
    interfaceAddress.can_addr.tp.tx_id = mReceiverOptions.mSourceCANId;
    interfaceAddress.can_addr.tp.rx_id = mReceiverOptions.mDestinationCANId;
    // Both source and destination are extended CANIDs
    if ( mReceiverOptions.mIsExtendedId )
    {
        interfaceAddress.can_addr.tp.tx_id |= CAN_EFF_FLAG;
        interfaceAddress.can_addr.tp.rx_id |= CAN_EFF_FLAG;
    }
    // Set the block size
    frameControlFlags.bs = mReceiverOptions.mBlockSize & 0xFF;
    // Set the Separation time
    frameControlFlags.stmin = mReceiverOptions.mFrameSeparationTimeMs & 0xFF;
    // Number of wait frames. Set to zero in FWE case as we can wait on reception.
    frameControlFlags.wftmax = 0x0;

    // Open a Socket in default mode ( Blocking )
    mSocket = socket( PF_CAN, SOCK_DGRAM /*| SOCK_NONBLOCK*/, CAN_ISOTP );
    if ( mSocket < 0 )
    {
        FWE_LOG_ERROR( "Failed to create the ISOTP Socket to IF: " + mReceiverOptions.mSocketCanIFName +
                       " Error: " + getErrnoString() );
        return false;
    }

    // Set the Frame Control Flags
    int retOptFlag = setsockopt( mSocket, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &optionalFlags, sizeof( optionalFlags ) );
    int retFrameCtrFlag =
        setsockopt( mSocket, SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, &frameControlFlags, sizeof( frameControlFlags ) );

    if ( ( retOptFlag < 0 ) || ( retFrameCtrFlag < 0 ) )
    {
        FWE_LOG_ERROR( "Failed to set ISO-TP socket option flags" );
        return false;
    }
    // CAN PF and Interface Index
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = static_cast<int>( if_nametoindex( mReceiverOptions.mSocketCanIFName.c_str() ) );

    // Bind the socket
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    if ( bind( mSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        FWE_LOG_ERROR( "Failed to bind the ISOTP Socket to IF: " + mReceiverOptions.mSocketCanIFName +
                       " Error: " + getErrnoString() );
        close( mSocket );
        return false;
    }
    FWE_LOG_TRACE( "ISOTP Socket connected to IF: " + mReceiverOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANReceiver::disconnect() const
{
    if ( close( mSocket ) < 0 )
    {
        FWE_LOG_ERROR( "Failed to disconnect the ISOTP Socket from IF: " + mReceiverOptions.mSocketCanIFName +
                       " Error: " + getErrnoString() );
        return false;
    }
    FWE_LOG_TRACE( "ISOTP Socket disconnected from IF: " + mReceiverOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANReceiver::isAlive() const
{
    int error = 0;
    socklen_t len = sizeof( error );
    // Get the error status of the socket
    int retSockOpt = getsockopt( mSocket, SOL_SOCKET, SO_ERROR, &error, &len );
    return ( ( retSockOpt == 0 ) && ( error == 0 ) );
}

bool
ISOTPOverCANReceiver::receivePDU( std::vector<uint8_t> &pduData )
{
    if ( mReceiverOptions.mP2TimeoutMs > P2_TIMEOUT_INFINITE )
    {
        struct pollfd pfd = { mSocket, POLLIN, 0 };
        int res = poll( &pfd, 1U, static_cast<int>( mReceiverOptions.mP2TimeoutMs ) );
        if ( res <= 0 )
        {
            // Error (<0) or timeout (==0):
            return false;
        }
    }
    // To pass on the vector to read, we need to reserve some bytes.
    pduData.resize( MAX_PDU_SIZE );
    // coverity[check_return : SUPPRESS]
    int bytesRead = static_cast<int>( read( mSocket, pduData.data(), MAX_PDU_SIZE ) );
    FWE_LOG_TRACE( "Received a PDU of size: " + std::to_string( bytesRead ) );
    // Remove the unnecessary bytes from the PDU container.
    if ( bytesRead > 0 )
    {
        pduData.resize( static_cast<size_t>( bytesRead ) );
    }
    else
    {
        pduData.resize( 0 );
    }

    return bytesRead > 0;
}

} // namespace IoTFleetWise
} // namespace Aws
