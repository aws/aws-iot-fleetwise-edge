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

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "businterfaces/SocketCANBusChannel.h"
#include "ClockHandler.h"
#include "TraceModule.h"
#include "datatypes/CANRawMessage.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <iostream>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>

#define DEFAULT_THREAD_IDLE_TIME_MS 1000
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
SocketCANBusChannel::SocketCANBusChannel( const std::string &socketCanInterfaceName, bool useKernelTimestamp )
{
    mResumeTime = 0;
    receivedMessages = 0;
    discardedMessages = 0;
    mType = NetworkChannelType::CAN_CHANNEL;
    mNetworkProtocol = NetworkChannelProtocol::RAW_SOCKET;
    mID = generateChannelID();
    mIfName = socketCanInterfaceName;
    mIdleTimeMs = DEFAULT_THREAD_IDLE_TIME_MS;
    mUseKernelTimestamp = useKernelTimestamp;
}

SocketCANBusChannel::~SocketCANBusChannel()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
SocketCANBusChannel::init( uint32_t bufferSize, uint32_t idleTimeMs )
{
    mCircularBuffPtr.reset( new CircularBuffer( bufferSize ) );
    mTimer.reset();
    if ( idleTimeMs != 0 )
    {
        mIdleTimeMs = idleTimeMs;
    }
    return true;
}

bool
SocketCANBusChannel::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    // Make sure the thread goes into sleep immediately to wait for
    // the manifest to be available
    mShouldSleep.store( true );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "SocketCANBusChannel::start", " Channel Thread failed to start " );
    }
    else
    {
        mLogger.trace( "SocketCANBusChannel::start", " Channel Thread started " );
        mThread.setThreadName( "fwVNLinuxCAN" + std::to_string( mID ) );
    }
    return mThread.isActive() && mThread.isValid();
}

void
SocketCANBusChannel::suspendDataAcquisition()
{
    // Go back to sleep
    mLogger.trace( "SocketCANBusChannel::suspendDataAcquisition",
                   "Going to sleep until a the resume signal. Channel : " + std::to_string( mID ) );
    mShouldSleep.store( true, std::memory_order_relaxed );
}

void
SocketCANBusChannel::resumeDataAcquisition()
{

    mLogger.trace( "SocketCANBusChannel::resumeDataAcquisition",
                   " Resuming Network data acquisition on Channel :" + std::to_string( mID ) );
    // Make sure the thread does not sleep anymore
    mResumeTime = mClock->timeSinceEpochMs();
    mShouldSleep.store( false );
    // Wake up the worker thread.
    mWait.notify();
}

bool
SocketCANBusChannel::stop()
{
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "SocketCANBusChannel::stop", " Channel Thread stopped " );
    return !mThread.isActive();
}

bool
SocketCANBusChannel::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
SocketCANBusChannel::shouldSleep() const
{
    return mShouldSleep.load( std::memory_order_relaxed );
}

void
SocketCANBusChannel::doWork( void *data )
{

    SocketCANBusChannel *channel = static_cast<SocketCANBusChannel *>( data );

    timestampT lastFrameTime = 0;
    uint32_t activations = 0;
    bool wokeUpFromSleep =
        false; /**< This variable is true after the thread is woken up for example because a valid decoder manifest was
                  received until the thread sleeps for the next time when it is false again*/
    Timer logTimer;
    do
    {
        activations++;
        if ( channel->shouldSleep() )
        {
            // We either just started or there was a decoder manifest update that we can't use
            // We should sleep
            channel->mLogger.trace( "SocketCANBusChannel::doWork",
                                    "No valid decoding dictionary available, Channel going to sleep " );
            channel->mWait.wait( Platform::Signal::WaitWithPredicate );
            wokeUpFromSleep = true;
        }

        channel->mTimer.reset();
        int nmsgs = 0;
        struct can_frame frame[PARALLEL_RECEIVED_FRAMES_FROM_KERNEL];
        struct iovec frame_buffer[PARALLEL_RECEIVED_FRAMES_FROM_KERNEL];
        struct mmsghdr msg[PARALLEL_RECEIVED_FRAMES_FROM_KERNEL];
        // we expect only one timestamp to return
        char cmsgReturnBuffer[PARALLEL_RECEIVED_FRAMES_FROM_KERNEL][CMSG_SPACE( sizeof( struct scm_timestamping ) )] = {
            { 0 } };

        // Setup all buffer to receive data
        for ( int i = 0; i < PARALLEL_RECEIVED_FRAMES_FROM_KERNEL; i++ )
        {
            frame_buffer[i].iov_base = &frame[i];
            frame_buffer[i].iov_len = sizeof( frame );
            msg[i].msg_hdr.msg_name = nullptr; // not interested in the source address
            msg[i].msg_hdr.msg_namelen = 0;
            msg[i].msg_hdr.msg_iov = &frame_buffer[i];
            msg[i].msg_hdr.msg_iovlen = 1;
            msg[i].msg_hdr.msg_control = &cmsgReturnBuffer[i];
            msg[i].msg_hdr.msg_controllen = sizeof( cmsgReturnBuffer[i] );
        }
        // In one syscall receive up to PARALLEL_RECEIVED_FRAMES_FROM_KERNEL frames in parallel
        nmsgs = recvmmsg( channel->mSocket, msg, PARALLEL_RECEIVED_FRAMES_FROM_KERNEL, 0, nullptr );
        for ( int i = 0; i < nmsgs; i++ )
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
            struct cmsghdr *currentHeader = CMSG_FIRSTHDR( &msg[i].msg_hdr );
            CANRawMessage message;
            timestampT timestamp = 0;
            if ( channel->mUseKernelTimestamp )
            {
                while ( currentHeader != nullptr )
                {
                    if ( currentHeader->cmsg_type == SO_TIMESTAMPING )
                    {
                        // With linux kernel 5.1 new return scm_timestamping64 was introduced
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                        scm_timestamping *timestampArray = (scm_timestamping *)( CMSG_DATA( currentHeader ) );
                        // From https://www.kernel.org/doc/Documentation/networking/can.txt
                        // Most timestamps are passed in ts[0]. Hardware timestamps are passed in ts[2].
                        if ( timestampArray->ts[2].tv_sec != 0 )
                        {
                            timestamp = static_cast<timestampT>( ( timestampArray->ts[2].tv_sec * 1000 ) +
                                                                 ( timestampArray->ts[2].tv_nsec / 1000000 ) );
                        }
                        else
                        {
                            timestamp = static_cast<timestampT>( ( timestampArray->ts[0].tv_sec * 1000 ) +
                                                                 ( timestampArray->ts[0].tv_nsec / 1000000 ) );
                        }
                    }
                    currentHeader = CMSG_NXTHDR( &msg[i].msg_hdr, currentHeader );
                }
                TraceModule::get().setVariable( MAX_SYSTEMTIME_KERNELTIME_DIFF,
                                                static_cast<uint64_t>( channel->mClock->timeSinceEpochMs() ) -
                                                    static_cast<uint64_t>( timestamp ) );
            }
            else
            {
                timestamp = channel->mClock->timeSinceEpochMs();
            }
            if ( timestamp < lastFrameTime )
            {
                TraceModule::get().incrementAtomicVariable( NOT_TIME_MONOTONIC_FRAMES );
            }
            // After waking up the Socket Can old messages in the kernel queue need to be ignored
            if ( !wokeUpFromSleep || timestamp >= channel->mResumeTime )
            {
                lastFrameTime = timestamp;
                channel->receivedMessages++;
                TraceVariable traceFrames = static_cast<TraceVariable>( channel->mID + READ_SOCKET_FRAMES_0 );
                TraceModule::get().setVariable( ( traceFrames < READ_SOCKET_FRAMES_MAX ) ? traceFrames
                                                                                         : READ_SOCKET_FRAMES_MAX,
                                                channel->receivedMessages );
                if ( message.setup( frame[i].can_id, frame[i].can_dlc, frame[i].data, timestamp ) == true )
                {
                    if ( !channel->mCircularBuffPtr->push( message ) )
                    {
                        channel->discardedMessages++;
                        TraceModule::get().setVariable( DISCARDED_FRAMES, channel->discardedMessages );
                        channel->mLogger.warn( "SocketCANBusChannel::doWork", " Circular Buffer is full" );
                    }
                }
                else
                {
                    channel->mLogger.warn( "SocketCANBusChannel::doWork", "Message is not valid" );
                }
            }
        }
        if ( nmsgs <= 0 )
        {
            if ( logTimer.getElapsedMs().count() > static_cast<int64_t>( LoggingModule::LOG_AGGREGATION_TIME_MS ) )
            {
                // Nothing is in the ring buffer to consume. Go to idle mode for some time.
                channel->mLogger.trace(
                    "SocketCANBusChannel::doWork",
                    "Activations: " + std::to_string( activations ) +
                        ". Waiting for some data to come. Idling for :" + std::to_string( channel->mIdleTimeMs ) +
                        " ms, processed " + std::to_string( channel->receivedMessages ) + " frames" );
                activations = 0;
                logTimer.reset();
            }
            channel->mWait.wait( static_cast<uint32_t>( channel->mIdleTimeMs ) );
            wokeUpFromSleep = false;
        }
    } while ( !channel->shouldStop() );
}

size_t
SocketCANBusChannel::queueSize()
{
    return mCircularBuffPtr->read_available();
}

bool
SocketCANBusChannel::connect()
{
    // Socket CAN parameters
    struct sockaddr_can interfaceAddress = {};
    struct ifreq interfaceRequest = {};
    // Open a Socket but make sure it's not blocking to not
    // cause a thread hang.
    int type = SOCK_RAW | SOCK_NONBLOCK;
    mSocket = socket( PF_CAN, type, CAN_RAW );
    if ( mSocket < 0 )
    {
        return false;
    }

    // Set the IF Name, address
    if ( mIfName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        return false;
    }
    (void)strncpy( interfaceRequest.ifr_name, mIfName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );

    if ( ioctl( mSocket, SIOCGIFINDEX, &interfaceRequest ) != 0 )
    {
        mLogger.error( "SocketCANBusChannel::connect", " SocketCan with name " + mIfName + " is not accessible" );
        close( mSocket );
        return false;
    }
    if ( mUseKernelTimestamp )
    {
        const int timestampFlags = ( SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE |
                                     SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE );
        if ( setsockopt( mSocket, SOL_SOCKET, SO_TIMESTAMPING, &timestampFlags, sizeof( timestampFlags ) ) != 0 )
        {
            mLogger.error( "SocketCANBusChannel::connect",
                           " Hardware timestamp not supported by socket but requested by config" );
            close( mSocket );
            return false;
        }
    }

    memset( &interfaceAddress, 0, sizeof( interfaceAddress ) );
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    // Bind the socket
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    if ( bind( mSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        close( mSocket );
        return false;
    }
    // Notify on connection success
    notifyListeners<const NetworkChannelID &>( &NetworkChannelBridgeListener::onNetworkChannelConnected, mID );
    // Start the main thread.
    return start();
}

bool
SocketCANBusChannel::disconnect()
{
    if ( !stop() && close( mSocket ) < 0 )
    {
        return false;
    }
    // Notify on connection closure
    notifyListeners<const NetworkChannelID &>( &NetworkChannelBridgeListener::onNetworkChannelDisconnected, mID );
    return true;
}

bool
SocketCANBusChannel::isAlive()
{
    int error = 0;
    socklen_t len = sizeof( error );
    // Get the error status of the socket
    int retSockOpt = getsockopt( mSocket, SOL_SOCKET, SO_ERROR, &error, &len );
    if ( retSockOpt == -1 || !mThread.isValid() || !mThread.isActive() || error != 0 )
    {
        return false;
    }
    return true;
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
