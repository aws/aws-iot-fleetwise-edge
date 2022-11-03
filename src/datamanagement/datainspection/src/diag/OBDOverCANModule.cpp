// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "OBDOverCANModule.h"
#include "EnumUtility.h"
#include "TraceModule.h"
#include <algorithm>
#include <csignal>
#include <ctime>
#include <iterator>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

constexpr int OBDOverCANModule::SLEEP_TIME_SECS;
constexpr uint32_t OBDOverCANModule::MASKING_GET_BYTE;       // Get last byte
constexpr uint32_t OBDOverCANModule::MASKING_SHIFT_BITS;     // Shift 8 bits
constexpr uint32_t OBDOverCANModule::MASKING_TEMPLATE_TX_ID; // All 29-bit tx id has the same bytes
constexpr uint32_t OBDOverCANModule::MASKING_REMOVE_BYTE;
constexpr uint32_t OBDOverCANModule::P2_TIMEOUT_DEFAULT_MS;

OBDOverCANModule::~OBDOverCANModule()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
OBDOverCANModule::init( SignalBufferPtr signalBufferPtr,
                        ActiveDTCBufferPtr activeDTCBufferPtr,
                        const std::string &gatewayCanInterfaceName,
                        const uint32_t &pidRequestIntervalSeconds,
                        const uint32_t &dtcRequestIntervalSeconds )
{
    // Sanity check
    if ( pidRequestIntervalSeconds == 0 && dtcRequestIntervalSeconds == 0 )
    {
        mLogger.trace( "OBDOverCANModule::init",
                       "Both PID and DTC interval seconds are set to 0. OBD module will not be initialized" );
        // We should not start the module if both intervals are zero
        return false;
    }

    if ( signalBufferPtr.get() == nullptr || activeDTCBufferPtr.get() == nullptr )
    {
        mLogger.error( "OBDOverCANModule::init", "Received Buffer nullptr" );
        return false;
    }
    else
    {
        mSignalBufferPtr = signalBufferPtr;
        mActiveDTCBufferPtr = activeDTCBufferPtr;
    }

    // Init the OBD Decoder
    mOBDDataDecoder = std::make_shared<OBDDataDecoder>();
    mGatewayCanInterfaceName = gatewayCanInterfaceName;
    mPIDRequestIntervalSeconds = pidRequestIntervalSeconds;
    mDTCRequestIntervalSeconds = dtcRequestIntervalSeconds;
    return true;
}

bool
OBDOverCANModule::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    // Make sure the thread goes into sleep immediately to wait for
    // the manifest to be available
    mDecoderManifestAvailable.store( false );
    // Do not request DTCs on startup
    mShouldRequestDTCs.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "OBDOverCANModule::start", " OBD Module Thread failed to start " );
    }
    else
    {
        mLogger.trace( "OBDOverCANModule::start", " OBD Module Thread started" );
        mThread.setThreadName( "fwDIOBDModule" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
OBDOverCANModule::stop()
{
    if ( !mThread.isValid() || !mThread.isActive() )
    {
        return true;
    }

    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mLogger.trace( "OBDOverCANModule::stop", " OBD Module Thread requested to stop " );
    mWait.notify();
    mDataAvailableWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "OBDOverCANModule::stop", " OBD Module Thread stopped " );
    return !mThread.isActive();
}

bool
OBDOverCANModule::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
OBDOverCANModule::doWork( void *data )
{
    OBDOverCANModule *obdModule = static_cast<OBDOverCANModule *>( data );
    // First we will auto detect ECUs
    bool finishECUsDetection = false;
    while ( !finishECUsDetection && !obdModule->shouldStop() )
    {
        std::vector<uint32_t> canIDResponses;
        // If we don't have an OBD decoder manifest and we should not request DTCs,
        // Take the thread to sleep
        if ( !obdModule->mShouldRequestDTCs.load( std::memory_order_relaxed ) &&
             ( !obdModule->mDecoderDictionaryPtr || obdModule->mDecoderDictionaryPtr->empty() ) )
        {
            obdModule->mLogger.trace(
                "OBDOverCANModule::doWork",
                "No valid decoding dictionary available and DTC requests disabled, Module Thread going to sleep " );
            obdModule->mDataAvailableWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        }
        // Now we will determine whether the ECUs are using extended IDs
        obdModule->autoDetectECUs( false, canIDResponses );
        if ( canIDResponses.empty() )
        {
            obdModule->autoDetectECUs( true, canIDResponses );
        }
        obdModule->mLogger.trace( "OBDOverCANModule::doWork",
                                  "Detect size of ECUs:" + std::to_string( canIDResponses.size() ) );
        if ( !canIDResponses.empty() )
        {
            // Initialize ECU for each CAN ID in canIDResponse
            if ( !obdModule->initECUs( canIDResponses ) )
            {
                // Failure from initECUs is non recoverable, hence we will send signal out to terminate program
                obdModule->mLogger.error( "OBDOverCANModule::doWork",
                                          "Fatal Error! OBDOverCANECU failed to init. Check CAN ISO-TP module" );
                std::raise( SIGUSR1 );
                return;
            }
            finishECUsDetection = true;
        }
        // As we haven't detected ECUs, wait for 1 second and try again
        else
        {
            obdModule->mLogger.trace( "OBDOverCANModule::doWork",
                                      " Waiting for :" + std::to_string( SLEEP_TIME_SECS ) + " seconds" );
            obdModule->mWait.wait( static_cast<uint32_t>( SLEEP_TIME_SECS * 1000 ) );
        }
    }

    obdModule->mDTCTimer.reset();
    obdModule->mPIDTimer.reset();
    // Flag to indicate whether we have acquired the supported PIDs from ECUs.
    bool hasAcquiredSupportedPIDs = false;
    while ( !obdModule->shouldStop() )
    {
        // Check if we need to request PIDs and we have a decoder manifest to decode them.
        if ( obdModule->mDecoderManifestAvailable.load( std::memory_order_relaxed ) )
        {
            // A new decoder manifest arrived. Pass it over to the OBD decoder.
            std::lock_guard<std::mutex> lock( obdModule->mDecoderDictMutex );
            obdModule->mOBDDataDecoder->setDecoderDictionary( obdModule->mDecoderDictionaryPtr );
            obdModule->mLogger.trace( "OBDOverCANModule::doWork", "Decoder Manifest set on the OBD Decoder " );
            // Reset the atomic state
            obdModule->mDecoderManifestAvailable.store( false, std::memory_order_relaxed );
        }
        // Request PID if decoder dictionary is valid
        if ( obdModule->mDecoderDictionaryPtr && !obdModule->mDecoderDictionaryPtr->empty() )
        {
            // Is it time to request PIDs ?
            // If so, send the requests then reschedule PID requests
            if ( obdModule->mPIDRequestIntervalSeconds > 0 &&
                 obdModule->mPIDTimer.getElapsedSeconds() >= obdModule->mPIDRequestIntervalSeconds )
            {
                // besides this thread, onChangeOfActiveDictionary can update mPIDsToRequestPerECU.
                // Use mutex to ensure only one thread is doing the update.
                std::lock_guard<std::mutex> lock( obdModule->mDecoderDictMutex );
                // This should execute only once
                if ( !hasAcquiredSupportedPIDs )
                {
                    hasAcquiredSupportedPIDs = obdModule->assignPIDsToECUs();
                }
                for ( auto ecu : obdModule->mECUs )
                {
                    ecu->requestReceiveEmissionPIDs( SID::CURRENT_STATS );
                }
                // Reschedule
                obdModule->mPIDTimer.reset();
            }
        }
        // Request DTC if specified by inspection matrix
        if ( obdModule->mShouldRequestDTCs.load( std::memory_order_relaxed ) )
        {
            bool successfulDTCRequest = false;
            DTCInfo dtcInfo;
            dtcInfo.receiveTime = obdModule->mClock->timeSinceEpochMs();
            // Request then reschedule DTC requests stored DTCs from each ECU
            if ( obdModule->mDTCRequestIntervalSeconds > 0 &&
                 obdModule->mDTCTimer.getElapsedSeconds() >= obdModule->mDTCRequestIntervalSeconds )
            {
                for ( auto ecu : obdModule->mECUs )
                {
                    successfulDTCRequest = ecu->getDTCData( dtcInfo );
                }
                // Also DTCInfo strutcs without any DTCs must be pushed to the queue because it means
                // there was a OBD request that did not return any SID::STORED_DTCs
                if ( successfulDTCRequest )
                {
                    // Note DTC buffer is a single producer single consumer queue. This is the only
                    // thread to push DTC Info to the queue
                    if ( !obdModule->mActiveDTCBufferPtr->push( dtcInfo ) )
                    {
                        obdModule->mLogger.warn( "OBDOverCANModule::doWork", "DTC Buffer full!" );
                    }
                }
                // Reschedule
                obdModule->mDTCTimer.reset();
            }
        }

        // Wait for the next cycle
        uint32_t sleepTime =
            obdModule->mDTCRequestIntervalSeconds > 0
                ? std::min( obdModule->mPIDRequestIntervalSeconds, obdModule->mDTCRequestIntervalSeconds )
                : obdModule->mPIDRequestIntervalSeconds;

        obdModule->mLogger.trace( "OBDOverCANModule::doWork",
                                  " Waiting for :" + std::to_string( sleepTime ) + " seconds" );
        obdModule->mWait.wait( static_cast<uint32_t>( sleepTime * 1000 ) );
    }
}

bool
OBDOverCANModule::autoDetectECUs( bool isExtendedID, std::vector<uint32_t> &canIDResponses )
{
    struct sockaddr_can interfaceAddress = {};
    struct ifreq interfaceRequest = {};
    struct can_frame frame = {};
    // After 1 second timeout and stop ECU detection
    constexpr uint32_t MAX_WAITING_MS = 1000;
    // Open a CAN raw socket
    int mSocket = socket( PF_CAN, SOCK_RAW, CAN_RAW );
    if ( mSocket < 0 )
    {
        mLogger.error( "OBDOverCANModule::autoDetectECUs", "Failed to create socket!" );
        return false;
    }
    // Set the IF name
    strncpy( interfaceRequest.ifr_name, mGatewayCanInterfaceName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1 );
    if ( ioctl( mSocket, SIOCGIFINDEX, &interfaceRequest ) != 0 )
    {
        close( mSocket );
        mLogger.error( "OBDOverCANModule::autoDetectECUs", "CAN interface is not accessible." );
        return false;
    }

    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    // Bind the socket
    if ( bind( mSocket, reinterpret_cast<struct sockaddr *>( &interfaceAddress ), sizeof( interfaceAddress ) ) < 0 )
    {
        close( mSocket );
        mLogger.error( "OBDOverCANModule::autoDetectECUs", "Failed to bind." );
        return false;
    }
    // Set frame can_id and flags
    frame.can_id = ( isExtendedID ) ? ( ( uint32_t )( ECUID::BROADCAST_EXTENDED_ID ) | CAN_EFF_FLAG )
                                    : (uint32_t)ECUID::BROADCAST_ID;
    frame.can_dlc = 8;  // CAN DLC
    frame.data[0] = 02; // Single frame data length
    frame.data[1] = 01; // Service ID 01: Show current data
    frame.data[2] = 00; // PID 00: PIDs supported [01-20]

    // Send broadcast request
    if ( write( mSocket, &frame, sizeof( struct can_frame ) ) != sizeof( struct can_frame ) )
    {
        close( mSocket );
        mLogger.error( "OBDOverCANModule::autoDetectECUs", "Failed to write." );
        return false;
    }
    mLogger.trace( "OBDOverCANModule::autoDetectECUs", "Sent broadcast request" );

    Timer broadcastTimer;
    broadcastTimer.reset();

    while ( !shouldStop() )
    {
        if ( broadcastTimer.getElapsedMs().count() > MAX_WAITING_MS )
        {
            mLogger.trace( "OBDOverCANModule::autoDetectECUs",
                           "Time elapsed:" + std::to_string( broadcastTimer.getElapsedMs().count() ) +
                               ", time to stop ECUs' detection" );
            break;
        }
        struct pollfd pfd = { mSocket, POLLIN, 0 };
        int res = poll( &pfd, 1U, static_cast<int>( OBDOverCANModule::P2_TIMEOUT_DEFAULT_MS ) );
        if ( res < 0 )
        {
            close( mSocket );
            mLogger.error( "OBDOverCANModule::autoDetectECUs", "Poll error." );
            return false;
        }
        if ( res == 0 ) // Time out
        {
            break;
        }
        // Get response
        if ( recv( mSocket, &frame, sizeof( struct can_frame ), 0 ) < 0 )
        {
            close( mSocket );
            mLogger.error( "OBDOverCANModule::autoDetectECUs", "Failed to read response." );
            return false;
        }

        // 11-bit range 7E8, 7E9, 7EA ... 7EF ==> [7E8, 7EF]
        // 29-bit range: [18DAF100, 18DAF1FF]
        auto frameCANId = ( isExtendedID ) ? ( frame.can_id & CAN_EFF_MASK ) : frame.can_id;
        auto smallestCANId = ( isExtendedID ) ? ( u_int32_t )( ECUID::LOWEST_ECU_EXTENDED_RX_ID )
                                              : ( u_int32_t )( ECUID::LOWEST_ECU_RX_ID );
        auto biggestCANId = ( isExtendedID ) ? ( u_int32_t )( ECUID::HIGHEST_ECU_EXTENDED_RX_ID )
                                             : ( u_int32_t )( ECUID::HIGHEST_ECU_RX_ID );
        // Check if CAN ID is valid
        if ( smallestCANId <= frameCANId && frameCANId <= biggestCANId )
        {
            canIDResponses.push_back( frameCANId );
        }
    }
    mLogger.trace( "OBDOverCANModule::autoDetectECUs",
                   "Detected number of ECUs:" + std::to_string( canIDResponses.size() ) );
    for ( std::size_t i = 0; i < canIDResponses.size(); ++i )
    {
        std::stringstream stream_rx;
        stream_rx << std::hex << canIDResponses[i];
        mLogger.trace( "OBDOverCANModule::autoDetectECUs", "ECU with rx_id: " + stream_rx.str() );
    }
    // Close the socket
    if ( close( mSocket ) < 0 )
    {
        mLogger.error( "OBDOverCANModule::autoDetectECUs", "Failed to close socket." );
        return false;
    }
    return true;
}

constexpr uint32_t
OBDOverCANModule::getTxIDByRxID( uint32_t rxId )
{
    // Calculate tx_id according to rx_id, operators are following:
    // If is 29-bit, e.g. rx_id = 0x18DAF159:
    //              rx_id & 0xFF = 0x59
    //              0x59 << 8 = 0x5900
    //              0x5900 | 0x18DA00F1 = 0x18DA59F1 = tx_id
    // If is 11-bit, e.g. rx_id = 0x7E8:
    //              rx_id - 0x8 = 0x7E0 = tx_id
    uint32_t txId = ( rxId > ( uint32_t )( ECUID::HIGHEST_ECU_RX_ID ) )
                        ? ( ( rxId & MASKING_GET_BYTE ) << MASKING_SHIFT_BITS ) | MASKING_TEMPLATE_TX_ID
                        : rxId - MASKING_REMOVE_BYTE;
    return txId;
}

bool
OBDOverCANModule::initECUs( std::vector<uint32_t> &canIDResponses )
{
    bool initStatus = true;
    // create a set of CAN ID in case there's duplication
    auto canIDSet = std::set<uint32_t>( canIDResponses.begin(), canIDResponses.end() );
    for ( auto canID : canIDSet )
    {
        auto obdOverCANECU = std::make_shared<OBDOverCANECU>();

        // Send physical request:
        auto rxID = canID;
        auto txID = getTxIDByRxID( rxID );

        // Check if CAN ID is 11-bit or 29-bit
        bool isExtendedId = ( (uint32_t)canID > ( uint32_t )( ECUID::HIGHEST_ECU_RX_ID ) ) ? true : false;
        if ( obdOverCANECU->init(
                 mGatewayCanInterfaceName, mOBDDataDecoder, rxID, txID, isExtendedId, mSignalBufferPtr ) )
        {
            mECUs.push_back( obdOverCANECU );
        }
        else
        {
            initStatus = false;
            mLogger.error( "OBDOverCANModule::initECUs",
                           "Failed to initialize OBDOverCANECU module for ECU " + std::to_string( rxID ) );
        }
    }
    mLogger.trace( "OBDOverCANModule::initialECUs", "Initialize ECUs in size of: " + std::to_string( mECUs.size() ) );
    return initStatus;
}

bool
OBDOverCANModule::assignPIDsToECUs()
{
    bool status = false;
    // clear the PID allocation table
    mPIDAssigned.clear();
    for ( auto &ecu : mECUs )
    {
        // Get supported PIDs. Edge agent will either request it from ECU or get it from the buffer
        if ( ecu->requestReceiveSupportedPIDs( SID::CURRENT_STATS ) )
        {
            // Allocate PID to each ECU to request. Note that if the PID has been already assigned, it will not be
            // reassigned to another ECU
            ecu->updatePIDRequestList(
                SID::CURRENT_STATS, mPIDsRequestedByDecoderDict[SID::CURRENT_STATS], mPIDAssigned );
            status = true;
        }
    }
    return status;
}

bool
OBDOverCANModule::connect()
{
    return start();
}

bool
OBDOverCANModule::disconnect()
{
    return stop();
}

bool
OBDOverCANModule::isAlive()
{
    if ( !mThread.isValid() || !mThread.isActive() )
    {
        return false;
    }
    for ( auto &ecu : mECUs )
    {
        if ( !ecu->isAlive() )
        {
            return false;
        }
    }
    return true;
}

void
OBDOverCANModule::onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &activeConditions )
{
    // We check here that at least one condition needs DTCs. If yes, we activate that.
    if ( activeConditions )
    {
        for ( auto const &condition : activeConditions->conditions )
        {
            if ( condition.includeActiveDtcs )
            {
                mShouldRequestDTCs.store( true, std::memory_order_relaxed );
                mDataAvailableWait.notify();
                mLogger.info( "OBDOverCANModule::onChangeInspectionMatrix", "Requesting DTC is enabled" );
                return;
            }
        }
        // If we are here, that means no condition had DTC collection active. So disabling DTC requests.
        mShouldRequestDTCs.store( false, std::memory_order_relaxed );
        // No need to notify, thread will pick it up in the next cycle.
    }
}

void
OBDOverCANModule::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                              VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol == VehicleDataSourceProtocol::OBD )
    {
        std::lock_guard<std::mutex> lock( mDecoderDictMutex );
        mDecoderDictionaryPtr = std::make_shared<OBDDecoderDictionary>();
        // Here we up cast the decoder dictionary to CAN Decoder Dictionary to extract can decoder method
        auto canDecoderDictionaryPtr = std::dynamic_pointer_cast<const CANDecoderDictionary>( dictionary );
        // As OBD only has one port, we expect the decoder dictionary only has one channel
        if ( mOBDDataDecoder != nullptr && canDecoderDictionaryPtr != nullptr )
        {
            // Iterate through the received generic decoder dictionary to construct the OBD specific dictionary
            std::vector<PID> pidsRequestedByDecoderDict{};
            if ( canDecoderDictionaryPtr->canMessageDecoderMethod.size() == 1 )
            {
                for ( const auto &canMessageDecoderMethod :
                      canDecoderDictionaryPtr->canMessageDecoderMethod.cbegin()->second )
                {
                    // The key is PID; The Value is decoder format
                    mDecoderDictionaryPtr->emplace( canMessageDecoderMethod.first,
                                                    canMessageDecoderMethod.second.format );
                    // Check if this PID's decoder method contains signals to be collected by
                    // Decoder Dictionary. If so, add the PID to pidsRequestedByDecoderDict
                    // Note in worst case scenario when no OBD signals are to be collected, this will
                    // iterate through the entire OBD signal lists which only contains a few hundreds signals.
                    for ( const auto &signal : canMessageDecoderMethod.second.format.mSignals )
                    {
                        // if the signal is to be collected according to decoder dictionary, push
                        // the corresponding PID to the pidsRequestedByDecoderDict
                        if ( canDecoderDictionaryPtr->signalIDsToCollect.find( signal.mSignalID ) !=
                             canDecoderDictionaryPtr->signalIDsToCollect.end() )
                        {
                            pidsRequestedByDecoderDict.emplace_back(
                                static_cast<PID>( canMessageDecoderMethod.first ) );
                            // We know this PID needs to be requested, break to move on next PID
                            break;
                        }
                    }
                }
            }
            std::sort( pidsRequestedByDecoderDict.begin(), pidsRequestedByDecoderDict.end() );
            mLogger.traceBytesInVector( "OBDOverCANModule::onChangeOfActiveDictionary",
                                        "Decoder Dictionary requests PIDs: ",
                                        pidsRequestedByDecoderDict );
            // For now we only support OBD Service Mode 1 PID
            mPIDsRequestedByDecoderDict[SID::CURRENT_STATS] = pidsRequestedByDecoderDict;

            // If the program already know the supported PIDs from ECU, below two update will update
            // For each ecu update the PIDs requested by the Dict
            mPIDAssigned.clear();
            for ( auto &ecu : mECUs )
            {
                ecu->updatePIDRequestList(
                    SID::CURRENT_STATS, mPIDsRequestedByDecoderDict[SID::CURRENT_STATS], mPIDAssigned );
            }

            // Pass on the decoder manifest to the OBD Decoder and wake up the thread.
            // Before that we should interrupt the thread so that no further decoding
            // is done using the previous decoder, then assign the new decoder manifest,
            // the wake up the thread.
            mDecoderManifestAvailable.store( true, std::memory_order_relaxed );
            // Wake up the worker thread.
            mDataAvailableWait.notify();
            mLogger.info( "OBDOverCANModule::onChangeOfActiveDictionary", "Decoder Manifest Updated" );
        }
        else
        {
            mLogger.warn( "OBDOverCANModule::onChangeOfActiveDictionary",
                          "Received Invalid Decoder Manifest, ignoring it" );
        }
    }
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
