// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <exception>
#include <iostream>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <memory>
#include <net/if.h>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#define MULTI_RX_SIZE 10

static volatile sig_atomic_t gRunning = 1;

static void
signalHandler( int signum )
{
    static_cast<void>( signum );
    gRunning = 0;
}

static uint16_t
stringToU16( const std::string &value )
{
    try
    {
        return static_cast<uint16_t>( std::stoul( value, nullptr, 0 ) );
    }
    catch ( const std::exception &e )
    {
        throw std::runtime_error( "Error parsing value " + value + " to uint16_t\n" );
    }
}

static uint64_t
extractTimestamp( struct msghdr *msgHeader )
{
    struct cmsghdr *currentHeader = CMSG_FIRSTHDR( msgHeader );
    uint64_t timestamp = 0;
    while ( currentHeader != nullptr )
    {
        if ( currentHeader->cmsg_type == SO_TIMESTAMPING )
        {
            // With linux kernel 5.1 new return scm_timestamping64 was introduced
            scm_timestamping *timestampArray = (scm_timestamping *)( CMSG_DATA( currentHeader ) );
            // From https://www.kernel.org/doc/Documentation/networking/timestamping.txt
            // Most timestamps are passed in ts[0]. Hardware timestamps are passed in ts[2].
            timestamp = static_cast<uint64_t>( ( static_cast<uint64_t>( timestampArray->ts[0].tv_sec ) * 1000000 ) +
                                               ( static_cast<uint64_t>( timestampArray->ts[0].tv_nsec ) / 1000 ) );
            break;
        }
        currentHeader = CMSG_NXTHDR( msgHeader, currentHeader );
    }
    if ( timestamp == 0 ) // other timestamp are invalid(=0)
    {
        timestamp =
            std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count();
    }
    return timestamp;
}

static int
openCanSocket( const std::string &interfaceName )
{
    std::cout << "Opening CAN interface " << interfaceName << "\n";
    struct sockaddr_can interfaceAddress = {};
    struct ifreq interfaceRequest = {};
    auto canSocket = socket( PF_CAN, SOCK_RAW, CAN_RAW );
    if ( canSocket < 0 )
    {
        std::cout << "Error creating CAN socket\n";
        return -1;
    }
    // Try to enable can_fd mode
    int canFdEnabled = 1;
    (void)setsockopt( canSocket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canFdEnabled, sizeof( canFdEnabled ) );

    // Set the IF Name, address
    if ( interfaceName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        std::cout << "Interface name too long\n";
        return -1;
    }
    (void)strncpy( interfaceRequest.ifr_name, interfaceName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );
    if ( ioctl( canSocket, SIOCGIFINDEX, &interfaceRequest ) != 0 )
    {
        std::cout << "CAN Interface with name " << interfaceName << " is not accessible\n";
        close( canSocket );
        return -1;
    }

    // Try to enable timestamping
    const int timestampFlags = ( SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE |
                                 SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE );
    (void)setsockopt( canSocket, SOL_SOCKET, SO_TIMESTAMPING, &timestampFlags, sizeof( timestampFlags ) );

    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    if ( bind( canSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        std::cout << "Failed to bind socket\n";
        close( canSocket );
        return -1;
    }
    return canSocket;
}

int
main( int argc, char **argv )
{
    try
    {
        std::cout << "can-to-someip\n";

        // clang-format off
        boost::program_options::options_description argsDescription(
            "Listen to CAN messages and offer them as a SOME/IP service\n"
            "The CAN data and metadata are sent as SOME/IP payload in the following format:\n"
            "___________________________________________________________\n"
            "|   CAN ID  |  Timestamp (in us)  |       CAN data        |\n"
            "|___________|_____________________|_______________________|\n"
            "   4 bytes         8 bytes             variable length\n"
            "CAN ID and Timestamp are unsigned integers encoded in network byte order (big endian).\n"
            "CAN ID is in the SocketCAN format: https://github.com/linux-can/can-utils/blob/88f0c753343bd863dd3110812d6b4698c4700b26/include/linux/can.h#L66-L78\n"
            "Options" );
        argsDescription.add_options()
            ( "can-interface",  boost::program_options::value<std::string>()->default_value( "vcan0" ),  "The CAN interface to listen to" )
            ( "service-id",     boost::program_options::value<std::string>()->default_value( "0x7777" ), "The service id that will be announced to other SOME/IP applications" )
            ( "instance-id",    boost::program_options::value<std::string>()->default_value( "0x5678" ), "The instance id of this service that will be announced. Only a single instance will be created." )
            ( "event-id",       boost::program_options::value<std::string>()->default_value( "0x8778" ), "ID of SOME/IP event that will be offered. All CAN data is sent with the same event ID." )
            ( "event-group-id", boost::program_options::value<std::string>()->default_value( "0x5555" ), "ID of SOME/IP event group that will be offered. Other applications will be able to subscribe to this event group." )
            ( "help", "Print this help message" );
        // clang-format on
        boost::program_options::variables_map args;
        boost::program_options::store( boost::program_options::parse_command_line( argc, argv, argsDescription ),
                                       args );
        boost::program_options::notify( args );
        if ( args.count( "help" ) > 0 )
        {
            std::cout << argsDescription << "\n";
            return 0;
        }

        std::string interfaceName = args["can-interface"].as<std::string>();
        uint16_t serviceId = stringToU16( args["service-id"].as<std::string>() );
        uint16_t instanceId = stringToU16( args["instance-id"].as<std::string>() );
        uint16_t eventId = stringToU16( args["event-id"].as<std::string>() );
        uint16_t eventGroupId = stringToU16( args["event-group-id"].as<std::string>() );

        struct sigaction signalAction = {};
        signalAction.sa_handler = signalHandler;
        sigaction( SIGINT, &signalAction, 0 );
        sigaction( SIGTERM, &signalAction, 0 );

        auto canSocket = openCanSocket( interfaceName );
        if ( canSocket < 0 )
        {
            return -1;
        }

        auto someipApp = vsomeip::runtime::get()->create_application( "can-to-someip" );
        someipApp->init();
        someipApp->offer_service( serviceId, instanceId );
        std::set<vsomeip::eventgroup_t> eventGroup;
        eventGroup.insert( eventGroupId );
        someipApp->offer_event( serviceId, instanceId, eventId, eventGroup, vsomeip::event_type_e::ET_FIELD );
        std::thread someipThread( [someipApp]() {
            someipApp->start();
        } );

        while ( gRunning != 0 )
        {
            // In one syscall receive up to MULTI_RX_SIZE frames at once
            struct canfd_frame frame[MULTI_RX_SIZE]
            {
            };
            struct iovec frame_buffer[MULTI_RX_SIZE]
            {
            };
            struct mmsghdr msg[MULTI_RX_SIZE]
            {
            };
            char cmsgReturnBuffer[MULTI_RX_SIZE][CMSG_SPACE( sizeof( struct scm_timestamping ) )]{};
            for ( int i = 0; i < MULTI_RX_SIZE; i++ )
            {
                frame_buffer[i].iov_base = &frame[i];
                frame_buffer[i].iov_len = sizeof( frame );
                msg[i].msg_hdr.msg_iov = &frame_buffer[i];
                msg[i].msg_hdr.msg_iovlen = 1;
                msg[i].msg_hdr.msg_control = &cmsgReturnBuffer[i];
                msg[i].msg_hdr.msg_controllen = sizeof( cmsgReturnBuffer[i] );
            }
            auto nmsgs = recvmmsg( canSocket, &msg[0], MULTI_RX_SIZE, 0, nullptr ); // blocking call
            if ( nmsgs < 0 )
            {
                break;
            }

            for ( int i = 0; i < nmsgs; i++ )
            {
                auto timestamp = extractTimestamp( &msg[i].msg_hdr );
                std::vector<uint8_t> data;
                uint32_t canIdBigEndian = htobe32( frame[i].can_id );
                uint64_t timestampBigEndian = htobe64( timestamp );
                data.insert( data.end(),
                             reinterpret_cast<uint8_t *>( &canIdBigEndian ),
                             reinterpret_cast<uint8_t *>( &canIdBigEndian ) + sizeof( canIdBigEndian ) );
                data.insert( data.end(),
                             reinterpret_cast<uint8_t *>( &timestampBigEndian ),
                             reinterpret_cast<uint8_t *>( &timestampBigEndian ) + sizeof( timestampBigEndian ) );
                data.insert( data.end(), frame[i].data, frame[i].data + frame[i].len );
                auto payload = vsomeip::runtime::get()->create_payload();
                payload->set_data( std::move( data ) );
                someipApp->notify( serviceId, instanceId, eventId, payload );
            }
        }
        someipApp->stop();
        someipThread.join();
        (void)close( canSocket );
        return 0;
    }
    catch ( const std::exception &e )
    {
        std::cout << e.what();
        return -1;
    }
}
