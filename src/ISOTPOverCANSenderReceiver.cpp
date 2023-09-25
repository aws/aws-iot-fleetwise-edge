// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ISOTPOverCANSenderReceiver.h"
#include "LoggingModule.h"
#include <cstdint>
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <poll.h>
#include <sstream>
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
        FWE_LOG_ERROR( "Failed to create the ISOTP rx id " + mStreamRxID +
                       " to IF:" + mSenderReceiverOptions.mSocketCanIFName + " Error: " + getErrnoString() );
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
    interfaceAddress.can_ifindex =
        static_cast<int>( if_nametoindex( mSenderReceiverOptions.mSocketCanIFName.c_str() ) );

    // Bind the socket
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    if ( bind( mSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        FWE_LOG_ERROR( " Failed to bind the ISOTP rx id " + mStreamRxID +
                       " to IF: " + mSenderReceiverOptions.mSocketCanIFName + " Error: " + getErrnoString() );
        close( mSocket );
        return false;
    }
    FWE_LOG_TRACE( "ISOTP rx id " + mStreamRxID + " connected to IF: " + mSenderReceiverOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANSenderReceiver::disconnect()
{
    if ( close( mSocket ) < 0 )
    {
        FWE_LOG_ERROR( "Failed to disconnect the ISOTP rx id " + mStreamRxID +
                       " from IF: " + mSenderReceiverOptions.mSocketCanIFName + " Error: " + getErrnoString() );
        return false;
    }
    FWE_LOG_TRACE( "ISOTP rx id " + mStreamRxID + " disconnected from IF: " + mSenderReceiverOptions.mSocketCanIFName );
    return true;
}

bool
ISOTPOverCANSenderReceiver::isAlive() const
{
    int error = 0;
    socklen_t len = sizeof( error );
    // Get the error status of the socket
    int retSockOpt = getsockopt( mSocket, SOL_SOCKET, SO_ERROR, &error, &len );
    return ( ( retSockOpt == 0 ) && ( error == 0 ) );
}

uint32_t
ISOTPOverCANSenderReceiver::flush( uint32_t timeout )
{
    struct pollfd pfd = { mSocket, POLLIN, 0 };
    // start time measurement
    Timer flushTimer;
    flushTimer.reset();
    int res = poll( &pfd, 1U, static_cast<int>( timeout ) );
    // end time measurement
    uint32_t pollNeededTime = static_cast<uint32_t>( flushTimer.getElapsedMs().count() );
    if ( res <= 0 )
    {
        return timeout;
    }
    std::vector<uint8_t> flushBuffer;
    flushBuffer.resize( MAX_PDU_SIZE );
    auto readRes = read( mSocket, flushBuffer.data(), MAX_PDU_SIZE );
    if ( readRes <= 0 )
    {
        FWE_LOG_ERROR( "Failed to read PDU from socket: " + mStreamRxID + " with error code " +
                       std::to_string( readRes ) );
    }
    return pollNeededTime;
}

bool
ISOTPOverCANSenderReceiver::receivePDU( std::vector<uint8_t> &pduData )
{
    if ( mSenderReceiverOptions.mP2TimeoutMs > P2_TIMEOUT_INFINITE )
    {
        struct pollfd pfd = { mSocket, POLLIN, 0 };
        int res = poll( &pfd, 1U, static_cast<int>( mSenderReceiverOptions.mP2TimeoutMs ) );
        if ( res == 0 )
        {
            // Responses are not always expected, so use trace level logging. E.g. supported PID requests for
            // unsupported PIDs can be ignored by some ECUs.
            FWE_LOG_TRACE( "Timeout reading PDU from socket: " + mStreamRxID );
            return false;
        }
        if ( res < 0 )
        {
            FWE_LOG_WARN( "Failed to read PDU from socket: " + mStreamRxID + " with error code " +
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
    FWE_LOG_TRACE( "Socket: " + mStreamRxID + " received a PDU of size " + std::to_string( bytesRead ) + ": " +
                   getStringFromBytes( pduData ) );

    return bytesRead > 0;
}

bool
ISOTPOverCANSenderReceiver::sendPDU( const std::vector<uint8_t> &pduData )
{
    auto socket = mSenderReceiverOptions.mBroadcastSocket < 0 ? mSocket : mSenderReceiverOptions.mBroadcastSocket;
    int bytesWritten = static_cast<int>( write( socket, pduData.data(), pduData.size() ) );
    FWE_LOG_TRACE( "Socket: " + mStreamRxID + " sent a PDU of size " + std::to_string( bytesWritten ) + ": " +
                   getStringFromBytes( pduData ) );
    return ( ( bytesWritten > 0 ) && ( bytesWritten == static_cast<int>( pduData.size() ) ) );
}

} // namespace IoTFleetWise
} // namespace Aws
