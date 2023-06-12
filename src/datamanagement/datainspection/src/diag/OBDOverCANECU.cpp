// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "OBDOverCANECU.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <sstream>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

// NOLINT below due to C++17 warning of redundant declarations that are required to maintain C++14 compatibility
constexpr size_t OBDOverCANECU::MAX_PID_RANGE; // NOLINT

bool
OBDOverCANECU::init( const std::string &gatewayCanInterfaceName,
                     const std::shared_ptr<OBDDataDecoder> &obdDataDecoder,
                     const uint32_t rxId,
                     const uint32_t txId,
                     bool isExtendedId,
                     SignalBufferPtr &signalBufferPtr,
                     int broadcastSocket )
{
    ISOTPOverCANSenderReceiverOptions optionsECU;
    optionsECU.mSocketCanIFName = gatewayCanInterfaceName;
    optionsECU.mSourceCANId = txId;
    optionsECU.mDestinationCANId = rxId;
    optionsECU.mIsExtendedId = isExtendedId;
    optionsECU.mBroadcastSocket = broadcastSocket;
    mOBDDataDecoder = obdDataDecoder;
    mSignalBufferPtr = signalBufferPtr;

    std::stringstream streamRx;
    streamRx << std::hex << rxId;
    mStreamRxID = streamRx.str();

    if ( mISOTPSenderReceiver.init( optionsECU ) && mISOTPSenderReceiver.connect() )
    {
        FWE_LOG_TRACE( "Successfully initialized ECU with ecu id: " + mStreamRxID );
    }
    else
    {
        FWE_LOG_ERROR( "Failed to initialize the ECU with ecu id: " + mStreamRxID );
        return false;
    }
    return true;
}

uint32_t
OBDOverCANECU::flush( uint32_t timeout )
{
    FWE_LOG_TRACE( "Flushed socket for ECU with ecu id: " + mStreamRxID );
    return mISOTPSenderReceiver.flush( timeout );
}

size_t
OBDOverCANECU::requestReceiveSupportedPIDs( const SID sid )
{
    size_t numRequests = 0;
    static_assert( supportedPIDRange.size() <= 8,
                   "Array length for supported PID range shall be less or equal than 8" );
    if ( mISOTPSenderReceiver.isAlive() )
    {
        if ( mSupportedPIDs.find( sid ) == mSupportedPIDs.end() )
        {
            SupportedPIDs allSupportedPIDs;
            FWE_LOG_TRACE( "Requesting Supported PIDs from the ECU " + mStreamRxID + " for SID " +
                           std::to_string( toUType( sid ) ) );

            // Request supported PID range. Per ISO 15765, we can only send six PID at one time
            auto pidList =
                std::vector<PID>( supportedPIDRange.begin(),
                                  supportedPIDRange.begin() + std::min( MAX_PID_RANGE, supportedPIDRange.size() ) );
            numRequests++;
            if ( requestPIDs( sid, pidList ) )
            {
                // Wait and process the response
                if ( !receiveSupportedPIDs( sid, allSupportedPIDs ) )
                {
                    TraceModule::get().incrementVariable( TraceVariable::OBD_ENG_PID_REQ_ERROR );
                    // log warning as all emissions-related OBD ECUs which support at least one of the
                    // services defined in J1979 shall support Service $01 and PID $00
                    FWE_LOG_WARN( "Failed to receive supported PID range from the ECU " + mStreamRxID );
                }
            }
            // check if we need to send out more PID range request
            if ( MAX_PID_RANGE < supportedPIDRange.size() )
            {
                pidList = std::vector<PID>( supportedPIDRange.begin() + MAX_PID_RANGE, supportedPIDRange.end() );
                numRequests++;
                if ( requestPIDs( sid, pidList ) )
                {
                    // Wait and process the response
                    receiveSupportedPIDs( sid, allSupportedPIDs );
                }
            }
            mSupportedPIDs.emplace( sid, allSupportedPIDs );
            FWE_LOG_TRACE( "ECU " + mStreamRxID + " supports PIDs for SID " + std::to_string( toUType( sid ) ) + ": " +
                           getStringFromBytes( allSupportedPIDs ) );
        }
    }
    return numRequests;
}

size_t
OBDOverCANECU::requestReceiveEmissionPIDs( const SID sid )
{
    EmissionInfo info;
    std::vector<PID> pids;
    // Request the PIDs ( up to 6 at a time )
    // To not overwhelm the bus, we split the PIDs into group of 6
    // and wait for the response.
    size_t numRequests = 0;
    if ( getRequestedPIDs( sid, pids ) && ( !pids.empty() ) )
    {
        SupportedPIDs::iterator pidItr = pids.begin();
        while ( pidItr != pids.end() )
        {
            requestReceivePIDs( pidItr, sid, pids, info );
        }

        pushPIDs( info, mClock->systemTimeSinceEpochMs(), mSignalBufferPtr, mStreamRxID );
    }
    return numRequests;
}

void
OBDOverCANECU::pushPIDs( const EmissionInfo &info,
                         Timestamp receptionTime,
                         SignalBufferPtr &signalBufferPtr,
                         const std::string &streamRxID )
{
    for ( auto const &signals : info.mPIDsToValues )
    {
        // Note Signal buffer is a multi producer single consumer queue. Besides current thread,
        // Vehicle Data Consumer will also push signals onto this buffer
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
        struct CollectedSignal collectedSignal;
        const auto signalType = signals.second.signalType;
        switch ( signalType )
        {
        case SignalType::UINT64:
            collectedSignal =
                CollectedSignal{ signals.first, receptionTime, signals.second.signalValue.uint64Val, signalType };
            FWE_LOG_TRACE( "Received Signal " + std::to_string( signals.first ) + " : " +
                           std::to_string( signals.second.signalValue.uint64Val ) + " for ECU: " + streamRxID );
            break;
        case SignalType::INT64:
            collectedSignal =
                CollectedSignal{ signals.first, receptionTime, signals.second.signalValue.int64Val, signalType };
            FWE_LOG_TRACE( "Received Signal " + std::to_string( signals.first ) + " : " +
                           std::to_string( signals.second.signalValue.int64Val ) + " for ECU: " + streamRxID );
            break;
        default:
            collectedSignal =
                CollectedSignal{ signals.first, receptionTime, signals.second.signalValue.doubleVal, signalType };
            FWE_LOG_TRACE( "Received Signal " + std::to_string( signals.first ) + " : " +
                           std::to_string( signals.second.signalValue.doubleVal ) + " for ECU: " + streamRxID );
            break;
        }

        if ( !signalBufferPtr->push( collectedSignal ) )
        {
            TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
            FWE_LOG_WARN( "Signal Buffer full with ECU " + streamRxID );
        }
    }
}

bool
OBDOverCANECU::getDTCData( DTCInfo &dtcInfo, size_t &numRequests )
{
    bool successfulDTCRequest = false;
    numRequests = 1;
    if ( requestReceiveDTCs( SID::STORED_DTC, dtcInfo ) )
    {
        successfulDTCRequest = true;
        FWE_LOG_TRACE( "Total number of DTCs: " + std::to_string( dtcInfo.mDTCCodes.size() ) );
    }
    else
    {
        FWE_LOG_WARN( "Failed to receive DTCs for ECU: " + mStreamRxID );
    }
    return successfulDTCRequest;
}

void
OBDOverCANECU::requestReceivePIDs( SupportedPIDs::iterator &pidItr,
                                   const SID sid,
                                   const SupportedPIDs &pids,
                                   EmissionInfo &info )
{
    std::vector<PID> currPIDs;
    while ( ( currPIDs.size() < MAX_PID_RANGE ) && ( pidItr != pids.end() ) )
    {
        currPIDs.push_back( *pidItr );
        pidItr++;
    }

    if ( requestPIDs( sid, currPIDs ) )
    {
        if ( receivePIDs( sid, currPIDs, info ) )
        {
            FWE_LOG_TRACE( "Received Emission PID data for SID: " + std::to_string( toUType( sid ) ) +
                           " with ECU: " + mStreamRxID );
        }
        else
        {
            TraceModule::get().incrementVariable( TraceVariable::OBD_ENG_PID_REQ_ERROR );
            FWE_LOG_WARN( "Failed to receive emission PID data for SID: " + std::to_string( toUType( sid ) ) +
                          " with ECU: " + mStreamRxID );
        }
    }
}

bool
OBDOverCANECU::receiveSupportedPIDs( const SID sid, SupportedPIDs &supportedPIDs )
{
    std::vector<uint8_t> ecuResponse;
    // Receive the PDU that has the Supported PIDs for this SID
    // decoded according to J1979 8.1.2.2
    if ( !mISOTPSenderReceiver.receivePDU( ecuResponse ) )
    {
        return false;
    }
    if ( ( !ecuResponse.empty() ) && mOBDDataDecoder->decodeSupportedPIDs( sid, ecuResponse, supportedPIDs ) )
    {
        return true;
    }
    FWE_LOG_WARN( "Failed to decode PDU for ECU " + mStreamRxID );
    return false;
}

bool
OBDOverCANECU::requestPIDs( const SID sid, const std::vector<PID> &pids )
{
    mTxPDU.clear();
    // Assume that the PIDs belong to the SID, and that they are
    // supported by the ECU
    // ECUs do not support more than 6 PIDs at a time.
    if ( pids.size() > MAX_PID_RANGE )
    {
        return false;
    }

    // First insert the SID
    mTxPDU.push_back( static_cast<uint8_t>( sid ) );
    // Then insert the items of the PIDs
    for ( auto pid : pids )
    {
        mTxPDU.push_back( pid );
    }
    FWE_LOG_TRACE( "Start to request emission PID data for SID: " + std::to_string( toUType( sid ) ) +
                   " with ECU: " + mStreamRxID );

    return mISOTPSenderReceiver.sendPDU( mTxPDU );
}

bool
OBDOverCANECU::receivePIDs( const SID sid, const std::vector<PID> &pids, EmissionInfo &info )
{
    std::vector<uint8_t> ecuResponse;
    // Receive the PDU that has the Supported PIDs for this SID
    // decoded according to J1979 8.1.2.2
    if ( !mISOTPSenderReceiver.receivePDU( ecuResponse ) )
    {
        FWE_LOG_WARN( "Failed to receive PDU for ECU " + mStreamRxID );
        return false;
    }
    // The info structure will be appended with the new decoded PIDs
    if ( !ecuResponse.empty() )
    {
        static_cast<void>( mOBDDataDecoder->decodeEmissionPIDs( sid, pids, ecuResponse, info ) );
    }
    else
    {
        FWE_LOG_WARN( "Failed to receive PID: ECU response is empty for ECU " + mStreamRxID );
        return false;
    }
    return true;
}

bool
OBDOverCANECU::requestDTCs( const SID sid )
{
    mTxPDU.clear();
    // Only SID is required for DTC requests
    mTxPDU.emplace_back( static_cast<uint8_t>( sid ) );
    // Send
    return mISOTPSenderReceiver.sendPDU( mTxPDU );
}

bool
OBDOverCANECU::receiveDTCs( const SID sid, DTCInfo &info )
{
    std::vector<uint8_t> ecuResponse;
    if ( !mISOTPSenderReceiver.receivePDU( ecuResponse ) )
    {
        return false;
    }
    // The info structure will be appended with the new decoded DTCs
    if ( ( !ecuResponse.empty() ) && mOBDDataDecoder->decodeDTCs( sid, ecuResponse, info ) )
    {
        return true;
    }

    return false;
}

bool
OBDOverCANECU::requestReceiveDTCs( const SID sid, DTCInfo &info )
{
    // Request and try to receive within the time interval
    if ( requestDTCs( sid ) )
    {
        // Wait and process the response
        if ( receiveDTCs( sid, info ) )
        {
            return true;
        }
    }
    else
    {
        FWE_LOG_WARN( "Can't request/receive ECU PIDs, with ECU: " + mStreamRxID +
                      " for SID: " + std::to_string( toUType( sid ) ) );
    }
    return false;
}

bool
OBDOverCANECU::getRequestedPIDs( const SID sid, std::vector<PID> &requestedPIDs ) const
{
    auto pidIterator = mPIDsToRequest.find( sid );
    if ( pidIterator == mPIDsToRequest.end() )
    {
        return false;
    }
    requestedPIDs = pidIterator->second;
    return true;
}

bool
OBDOverCANECU::isAlive() const
{
    return mISOTPSenderReceiver.isAlive();
}

void
OBDOverCANECU::updatePIDRequestList( const SID sid,
                                     const std::vector<PID> &pidsRequestedByDecoderDict,
                                     std::unordered_set<PID> &pidAssigned )
{
    if ( mSupportedPIDs.find( sid ) != mSupportedPIDs.end() )
    {
        // Update the PID Request List with PIDs that are common between decoder dictionary and the PIDs supported by
        // ECU
        std::vector<PID> pidIntersectionList{};
        // Note that the two vector has to be sorted previously to use the function below properly
        std::set_intersection( mSupportedPIDs[sid].begin(),
                               mSupportedPIDs[sid].end(),
                               pidsRequestedByDecoderDict.begin(),
                               pidsRequestedByDecoderDict.end(),
                               std::back_inserter( pidIntersectionList ) );
        std::vector<PID> pidsToRequest{};
        // If the PID has been already allocated to other ECUs, we will not include this PID to the current ECU
        std::copy_if( pidIntersectionList.begin(),
                      pidIntersectionList.end(),
                      std::back_inserter( pidsToRequest ),
                      [&]( PID pid ) -> bool {
                          if ( pidAssigned.count( pid ) == 0 )
                          {
                              pidAssigned.insert( pid );
                              return true;
                          }
                          else
                          {
                              return false;
                          }
                      } );
        FWE_LOG_TRACE( "The PIDs to request from " + mStreamRxID + " are: " + getStringFromBytes( pidsToRequest ) );
        mPIDsToRequest[sid] = pidsToRequest;
    }
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
