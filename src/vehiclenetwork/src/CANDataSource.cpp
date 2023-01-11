// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "businterfaces/CANDataSource.h"
#include "ClockHandler.h"
#include "EnumUtility.h"
#include "TraceModule.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <cstring>
#include <iostream>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <net/if.h>
#include <sys/ioctl.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
using namespace Aws::IoTFleetWise::Platform::Utility;
static const std::string INTERFACE_NAME_KEY = "interfaceName";
static const std::string THREAD_IDLE_TIME_KEY = "threadIdleTimeMs";
static const std::string PROTOCOL_NAME_KEY = "protocolName";
CANDataSource::CANDataSource( CAN_TIMESTAMP_TYPE timestampTypeToUse )
    : mTimestampTypeToUse{ timestampTypeToUse }
{
    mType = VehicleDataSourceType::CAN_SOURCE;
    mNetworkProtocol = VehicleDataSourceProtocol::RAW_SOCKET;
    mID = generateSourceID();
}

CANDataSource::CANDataSource()
{
    mType = VehicleDataSourceType::CAN_SOURCE;
    mNetworkProtocol = VehicleDataSourceProtocol::RAW_SOCKET;
    mID = generateSourceID();
}

CANDataSource::~CANDataSource()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
CANDataSource::init( const std::vector<VehicleDataSourceConfig> &sourceConfigs )
{
    // Only one source config is supported on the CAN stack i.e. we manage one socket with
    // one single thread.
    if ( ( sourceConfigs.size() > 1 ) || sourceConfigs.empty() )
    {
        mLogger.error( "CANDataSource::init", "Only one source config is supported" );
        return false;
    }
    auto settingsIterator = sourceConfigs[0].transportProperties.find( std::string( INTERFACE_NAME_KEY ) );
    if ( settingsIterator == sourceConfigs[0].transportProperties.end() )
    {
        mLogger.error( "CANDataSource::init", "Could not find interfaceName in the config" );
        return false;
    }
    else
    {
        mIfName = settingsIterator->second;
    }

    mCircularBuffPtr =
        std::make_shared<VehicleMessageCircularBuffer>( sourceConfigs[0].maxNumberOfVehicleDataMessages );
    settingsIterator = sourceConfigs[0].transportProperties.find( std::string( THREAD_IDLE_TIME_KEY ) );
    if ( settingsIterator == sourceConfigs[0].transportProperties.end() )
    {
        mLogger.error( "CANDataSource::init", "Could not find threadIdleTimeMs in the config" );
        return false;
    }
    else
    {
        try
        {
            mIdleTimeMs = static_cast<uint32_t>( std::stoul( settingsIterator->second ) );
        }
        catch ( const std::exception &e )
        {
            mLogger.error( "CANDataSource::init",
                           "Could not cast the threadIdleTimeMs, invalid input: " + std::string( e.what() ) );
            return false;
        }
    }

    settingsIterator = sourceConfigs[0].transportProperties.find( std::string( PROTOCOL_NAME_KEY ) );
    if ( settingsIterator == sourceConfigs[0].transportProperties.end() )
    {
        mLogger.error( "CANDataSource::init", "Could not find protocolName in the config" );
        return false;
    }
    else
    {
        mForceCanFD = settingsIterator->second == "CAN-FD";
    }

    mTimer.reset();
    return true;
}

bool
CANDataSource::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    // Make sure the thread goes into sleep immediately to wait for
    // the manifest to be available
    mShouldSleep.store( true );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "CANDataSource::start", "Thread failed to start" );
    }
    else
    {
        mLogger.trace( "CANDataSource::start", "Thread started" );
        mThread.setThreadName( "fwVNLinuxCAN" + std::to_string( mID ) );
    }
    return mThread.isActive() && mThread.isValid();
}

void
CANDataSource::suspendDataAcquisition()
{
    // Go back to sleep
    mLogger.trace( "CANDataSource::suspendDataAcquisition",
                   "Going to sleep until a the resume signal. CAN Data Source: " + std::to_string( mID ) );
    mShouldSleep.store( true, std::memory_order_relaxed );
}

void
CANDataSource::resumeDataAcquisition()
{

    mLogger.trace( "CANDataSource::resumeDataAcquisition",
                   "Resuming Network data acquisition on Data Source: " + std::to_string( mID ) );
    // Make sure the thread does not sleep anymore
    mResumeTime = mClock->systemTimeSinceEpochMs();
    mShouldSleep.store( false );
    // Wake up the worker thread.
    mWait.notify();
}

bool
CANDataSource::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "CANDataSource::stop", "Thread stopped" );
    return !mThread.isActive();
}

bool
CANDataSource::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
CANDataSource::shouldSleep() const
{
    return mShouldSleep.load( std::memory_order_relaxed );
}

Timestamp
CANDataSource::extractTimestamp( struct msghdr *msgHeader )
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    struct cmsghdr *currentHeader = CMSG_FIRSTHDR( msgHeader );
    Timestamp timestamp = 0;
    if ( mTimestampTypeToUse != CAN_TIMESTAMP_TYPE::POLLING_TIME )
    {
        while ( currentHeader != nullptr )
        {
            if ( currentHeader->cmsg_type == SO_TIMESTAMPING )
            {
                // With linux kernel 5.1 new return scm_timestamping64 was introduced
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                scm_timestamping *timestampArray = (scm_timestamping *)( CMSG_DATA( currentHeader ) );
                // From https://www.kernel.org/doc/Documentation/networking/timestamping.txt
                // Most timestamps are passed in ts[0]. Hardware timestamps are passed in ts[2].
                if ( mTimestampTypeToUse == CAN_TIMESTAMP_TYPE::KERNEL_HARDWARE_TIMESTAMP )
                {
                    timestamp =
                        static_cast<Timestamp>( ( static_cast<Timestamp>( timestampArray->ts[2].tv_sec ) * 1000 ) +
                                                ( static_cast<Timestamp>( timestampArray->ts[2].tv_nsec ) / 1000000 ) );
                }
                else if ( mTimestampTypeToUse == CAN_TIMESTAMP_TYPE::KERNEL_SOFTWARE_TIMESTAMP ) // default
                {
                    timestamp =
                        static_cast<Timestamp>( ( static_cast<Timestamp>( timestampArray->ts[0].tv_sec ) * 1000 ) +
                                                ( static_cast<Timestamp>( timestampArray->ts[0].tv_nsec ) / 1000000 ) );
                }
            }
            currentHeader = CMSG_NXTHDR( msgHeader, currentHeader );
        }
        TraceModule::get().setVariable( TraceVariable::MAX_SYSTEMTIME_KERNELTIME_DIFF,
                                        static_cast<uint64_t>( mClock->systemTimeSinceEpochMs() ) -
                                            static_cast<uint64_t>( timestamp ) );
    }
    if ( timestamp == 0 ) // either other timestamp are invalid(=0) or mTimestampTypeToUse == POLLING_TIME
    {
        TraceModule::get().incrementVariable( TraceVariable::CAN_POLLING_TIMESTAMP_COUNTER );
        timestamp = mClock->systemTimeSinceEpochMs();
    }
    return timestamp;
}

void
CANDataSource::doWork( void *data )
{

    CANDataSource *dataSource = static_cast<CANDataSource *>( data );

    Timestamp lastFrameTime = 0;
    uint32_t activations = 0;
    bool wokeUpFromSleep =
        false; /**< This variable is true after the thread is woken up for example because a valid decoder manifest was
                  received until the thread sleeps for the next time when it is false again*/
    Timer logTimer;
    do
    {
        activations++;
        if ( dataSource->shouldSleep() )
        {
            // We either just started or there was a decoder manifest update that we can't use
            // We should sleep
            dataSource->mLogger.trace( "CANDataSource::doWork",
                                       "No valid decoding dictionary available, channel going to sleep" );
            dataSource->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
            wokeUpFromSleep = true;
        }

        dataSource->mTimer.reset();
        int nmsgs = 0;
        struct canfd_frame frame[PARALLEL_RECEIVED_FRAMES_FROM_KERNEL];
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
        nmsgs = recvmmsg( dataSource->mSocket, msg, PARALLEL_RECEIVED_FRAMES_FROM_KERNEL, 0, nullptr );
        for ( int i = 0; i < nmsgs; i++ )
        {
            VehicleDataMessage message;
            const std::vector<boost::any> syntheticData{};
            std::vector<std::uint8_t> rawData = {};
            Timestamp timestamp = dataSource->extractTimestamp( &msg[i].msg_hdr );
            if ( timestamp < lastFrameTime )
            {
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::NOT_TIME_MONOTONIC_FRAMES );
            }
            // After waking up the Socket Can old messages in the kernel queue need to be ignored
            if ( ( !wokeUpFromSleep ) || ( timestamp >= dataSource->mResumeTime ) )
            {
                lastFrameTime = timestamp;
                dataSource->receivedMessages++;
                TraceVariable traceFrames =
                    static_cast<TraceVariable>( dataSource->mID + toUType( TraceVariable::READ_SOCKET_FRAMES_0 ) );
                TraceModule::get().setVariable( ( traceFrames < TraceVariable::READ_SOCKET_FRAMES_MAX )
                                                    ? traceFrames
                                                    : TraceVariable::READ_SOCKET_FRAMES_MAX,
                                                dataSource->receivedMessages );
                rawData.reserve( frame[i].len );
                for ( size_t j = 0; j < frame[i].len; ++j )
                {
                    rawData.emplace_back( frame[i].data[j] );
                }
                message.setup( frame[i].can_id, rawData, syntheticData, timestamp );
                if ( message.isValid() )
                {
                    if ( !dataSource->mCircularBuffPtr->push( message ) )
                    {
                        dataSource->discardedMessages++;
                        TraceModule::get().setVariable( TraceVariable::DISCARDED_FRAMES,
                                                        dataSource->discardedMessages );
                        dataSource->mLogger.warn( "CANDataSource::doWork", "Circular Buffer is full" );
                    }
                }
                else
                {
                    dataSource->mLogger.warn( "CANDataSource::doWork", "Message is not valid" );
                }
            }
        }
        if ( nmsgs <= 0 )
        {
            if ( logTimer.getElapsedMs().count() > static_cast<int64_t>( LoggingModule::LOG_AGGREGATION_TIME_MS ) )
            {
                // Nothing is in the ring buffer to consume. Go to idle mode for some time.
                dataSource->mLogger.trace(
                    "CANDataSource::doWork",
                    "Activations: " + std::to_string( activations ) +
                        ". Waiting for some data to come. Idling for :" + std::to_string( dataSource->mIdleTimeMs ) +
                        " ms, processed " + std::to_string( dataSource->receivedMessages ) + " frames" );
                activations = 0;
                logTimer.reset();
            }
            dataSource->mWait.wait( static_cast<uint32_t>( dataSource->mIdleTimeMs ) );
            wokeUpFromSleep = false;
        }
    } while ( !dataSource->shouldStop() );
}

bool
CANDataSource::connect()
{
    // Socket CAN parameters
    struct sockaddr_can interfaceAddress = {};
    struct ifreq interfaceRequest = {};
    // Open a Socket but make sure it's not blocking to not
    // cause a thread hang.
    int canfd_on = 1;
    int type = SOCK_RAW | SOCK_NONBLOCK;
    mSocket = socket( PF_CAN, type, CAN_RAW );
    if ( mSocket < 0 )
    {
        return false;
    }
    // Switch Socket can_fd mode on or fallback with a log if it fails
    if ( setsockopt( mSocket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof( canfd_on ) ) != 0 )
    {
        if ( mForceCanFD )
        {
            mLogger.error( "CANDataSource::connect", "setsockopt CAN_RAW_FD_FRAMES FAILED" );
            return false;
        }
        else
        {
            mLogger.info( "CANDataSource::connect",
                          "setsockopt CAN_RAW_FD_FRAMES FAILED, falling back to regular CAN" );
        }
    }

    // Set the IF Name, address
    if ( mIfName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        return false;
    }
    (void)strncpy( interfaceRequest.ifr_name, mIfName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );

    if ( ioctl( mSocket, SIOCGIFINDEX, &interfaceRequest ) != 0 )
    {
        mLogger.error( "CANDataSource::connect", "CAN Interface with name " + mIfName + " is not accessible" );
        close( mSocket );
        return false;
    }
    if ( ( mTimestampTypeToUse == CAN_TIMESTAMP_TYPE::KERNEL_SOFTWARE_TIMESTAMP ) ||
         ( mTimestampTypeToUse == CAN_TIMESTAMP_TYPE::KERNEL_HARDWARE_TIMESTAMP ) )
    {
        const int timestampFlags = ( SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE |
                                     SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE );
        if ( setsockopt( mSocket, SOL_SOCKET, SO_TIMESTAMPING, &timestampFlags, sizeof( timestampFlags ) ) != 0 )
        {
            mLogger.error( "CANDataSource::connect",
                           "Hardware timestamp not supported by socket but requested by config" );
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
    notifyListeners<const VehicleDataSourceID &>( &VehicleDataSourceListener::onVehicleDataSourceConnected, mID );
    // Start the main thread.
    return start();
}

bool
CANDataSource::disconnect()
{
    if ( ( !stop() ) && ( close( mSocket ) < 0 ) )
    {
        return false;
    }
    // Notify on connection closure
    notifyListeners<const VehicleDataSourceID &>( &VehicleDataSourceListener::onVehicleDataSourceDisconnected, mID );
    return true;
}

bool
CANDataSource::isAlive()
{
    int error = 0;
    socklen_t len = sizeof( error );
    // Get the error status of the socket
    int retSockOpt = getsockopt( mSocket, SOL_SOCKET, SO_ERROR, &error, &len );
    if ( ( retSockOpt == -1 ) || ( !mThread.isValid() ) || ( !mThread.isActive() ) || ( error != 0 ) )
    {
        return false;
    }
    return true;
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
