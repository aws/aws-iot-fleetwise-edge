// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "OBDOverCANModule.h"
#include "EnumUtility.h"
#include "OBDDataTypes.h"
#include "TraceModule.h"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <iostream>
#include <iterator>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

constexpr size_t OBDOverCANModule::MAX_PID_RANGE;

OBDOverCANModule::OBDOverCANModule()
{
    mOBDHeartBeatIntervalSeconds = 0;
    mPIDRequestIntervalSeconds = 0;
    mDTCRequestIntervalSeconds = 0;
    mHasTransmission = false;
    mVIN.clear();
}

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
                        const uint32_t &dtcRequestIntervalSeconds,
                        const bool &useExtendedIDs,
                        const bool &hasTransmissionECU )
{
    // Sanity check
    if ( pidRequestIntervalSeconds == 0 && dtcRequestIntervalSeconds == 0 )
    {
        // We should not start the module if both intervals are zero
        return false;
    }
    // Establish a bi-directional P2P channel on ISO-TP between FWE, Engine ECU and Transmission ECU.
    // Each ECU listens on the bus for their RX IDs, and respond with their TX IDs.
    ISOTPOverCANSenderReceiverOptions optionsEngine;
    ISOTPOverCANSenderReceiverOptions optionsTransmission;
    optionsEngine.mSocketCanIFName = gatewayCanInterfaceName;
    optionsTransmission.mSocketCanIFName = gatewayCanInterfaceName;

    if ( useExtendedIDs )
    {
        // ECM
        optionsEngine.mIsExtendedId = true;
        optionsEngine.mSourceCANId = toUType( ECUID::ENGINE_ECU_TX_EXTENDED );
        optionsEngine.mDestinationCANId = toUType( ECUID::ENGINE_ECU_RX_EXTENDED );
        // TCM
        optionsTransmission.mIsExtendedId = true;
        optionsTransmission.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_TX_EXTENDED );
        optionsTransmission.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_RX_EXTENDED );
    }
    else
    {
        // ECM
        optionsEngine.mSourceCANId = toUType( ECUID::ENGINE_ECU_TX );
        optionsEngine.mDestinationCANId = toUType( ECUID::ENGINE_ECU_RX );
        // TCM
        optionsTransmission.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_TX );
        optionsTransmission.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_RX );
    }

    mPIDRequestIntervalSeconds = pidRequestIntervalSeconds;
    mDTCRequestIntervalSeconds = dtcRequestIntervalSeconds;

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
    mOBDDataDecoder = std::make_unique<OBDDataDecoder>();

    // Init the Transmission ECU
    if ( hasTransmissionECU )
    {
        if ( mTransmissionECUSenderReceiver.init( optionsTransmission ) )
        {
            mLogger.trace( "OBDOverCANModule::init", "Transmission ECU Initialized" );
            mHasTransmission = true;
        }
        else
        {
            mLogger.error( "OBDOverCANModule::init", "Failed to the Transmission ECU" );
            mHasTransmission = false;
            return false;
        }
    }
    // Init the Engine ECU
    if ( mEngineECUSenderReceiver.init( optionsEngine ) )
    {

        mLogger.trace( "OBDOverCANModule::init", "Engine ECU Initialized" );
        mLogger.info( "OBDOverCANModule::init", "OBD Module Initialized" );
    }
    else
    {
        mLogger.error( "OBDOverCANModule::init", "Failed to init the Engine ECU" );
        return false;
    }

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
    OBDOverCANModule *OBDModule = static_cast<OBDOverCANModule *>( data );

    OBDModule->mDTCTimer.reset();
    OBDModule->mPIDTimer.reset();
    while ( !OBDModule->shouldStop() )
    {

        // If we don't have an OBD decoder manifest and we should not request DTCs,
        // Take the thread to sleep
        if ( !OBDModule->mShouldRequestDTCs.load( std::memory_order_relaxed ) &&
             ( !OBDModule->mDecoderDictionaryPtr || OBDModule->mDecoderDictionaryPtr->empty() ) )
        {
            OBDModule->mLogger.trace(
                "OBDOverCANModule::doWork",
                "No valid decoding dictionary available and DTC requests disabled, Module Thread going to sleep " );
            OBDModule->mDataAvailableWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        }

        // Check if we need to request PIDs and we have a decoder manifest to decode them.
        if ( OBDModule->mDecoderManifestAvailable.load( std::memory_order_relaxed ) )
        {
            // A new decoder manifest arrived. Pass it over to the OBD decoder.
            {
                std::lock_guard<std::mutex> lock( OBDModule->mDecoderDictMutex );
                OBDModule->mOBDDataDecoder->setDecoderDictionary( OBDModule->mDecoderDictionaryPtr );
                OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Decoder Manifest set on the OBD Decoder " );
                // Reset the atomic state
                OBDModule->mDecoderManifestAvailable.store( false, std::memory_order_relaxed );
            }
        }

        // Thread woken up. Execute the PID request flow if activated.
        if ( OBDModule->mDecoderDictionaryPtr && !OBDModule->mDecoderDictionaryPtr->empty() )
        {
            // Start by requesting the VIN then
            // Check whether the ECUs reported their support PIDs.
            // If not, request and process them.
            // We need to consolidate these functions into a generic implementation that
            // takes the ECUType as an input. Currently we are limited to 2 ECUs only.
            SupportedPIDs enginePIDs;
            SupportedPIDs transmissionPIDs;
            EmissionInfo enginePIDInfo;
            EmissionInfo transmissionPIDInfo;
            ECUDiagnosticInfo transmissionDiagnosticInfo;
            ECUDiagnosticInfo engineDiagnosticInfo;
            engineDiagnosticInfo.mEcuType = ECUType::ENGINE;
            transmissionDiagnosticInfo.mEcuType = ECUType::TRANSMISSION;
            // Check if the VIN has been already successfully requested.
            // If not, request it and store it.
            // The request always goes to the ECM, even though the VIN is also available
            // in other ECUs.
            if ( OBDModule->mVIN.empty() )
            {
                OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Requesting VIN from Engine ECU" );
                if ( OBDModule->requestReceiveVIN( OBDModule->mVIN ) )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Received VIN from Engine ECU" );
                }
                else
                {
                    TraceModule::get().incrementVariable( TraceVariable::OBD_VIN_ERROR );
                    OBDModule->mLogger.error( "OBDOverCANModule::doWork", "Failed to receive VIN from Engine ECU" );
                }
            }

            // Set the VIN on the diagnostic infos
            engineDiagnosticInfo.mVIN = OBDModule->mVIN;
            transmissionDiagnosticInfo.mVIN = OBDModule->mVIN;
            // Is it time to request PIDs ?
            // If so, send the requests then reschedule PID requests
            if ( OBDModule->mPIDRequestIntervalSeconds > 0 &&
                 OBDModule->mPIDTimer.getElapsedSeconds() >= OBDModule->mPIDRequestIntervalSeconds )
            {
                // Engine ECU
                if ( OBDModule->mEngineECUSenderReceiver.isAlive() && OBDModule->mSupportedPIDsEngine.empty() )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Requesting Supported PIDs from Engine ECU" );
                    // Send the Supported PID request
                    SupportedPIDs enginePIDs;
                    if ( OBDModule->requestReceiveSupportedPIDs(
                             SID::CURRENT_STATS, OBDModule->mEngineECUSenderReceiver, enginePIDs ) )
                    {
                        std::sort( enginePIDs.begin(), enginePIDs.end() );
                        OBDModule->mSupportedPIDsEngine.emplace( SID::CURRENT_STATS, enginePIDs );
                        std::ostringstream oss;
                        oss << ": ";
                        if ( !enginePIDs.empty() )
                        {
                            std::copy(
                                enginePIDs.begin(), enginePIDs.end() - 1, std::ostream_iterator<int>( oss, "," ) );
                            oss << std::to_string( enginePIDs.back() );
                        }
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                  "Engine ECU supports PIDs for SID " +
                                                      std::to_string( toUType( SID::CURRENT_STATS ) ) + oss.str() );
                        // Take the common PIDs between the ECU supported PIDs and the PIDs that are requested by
                        // Decoder Dictionary
                        OBDModule->updatePIDRequestList( SID::CURRENT_STATS,
                                                         ECUType::ENGINE,
                                                         OBDModule->mSupportedPIDsEngine,
                                                         OBDModule->mPIDsToRequestEngine );
                    }
                    else
                    {
                        TraceModule::get().incrementVariable( TraceVariable::OBD_ENG_PID_REQ_ERROR );
                        OBDModule->mLogger.error( "OBDOverCANModule::doWork",
                                                  "Failed to request/receive Engine ECU PIDs for SID: " +
                                                      std::to_string( toUType( SID::CURRENT_STATS ) ) );
                    }
                }
                // Transmission ECU
                if ( OBDModule->mHasTransmission && OBDModule->mSupportedPIDsTransmission.empty() )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                              "Requesting Supported PIDs from Transmission ECU" );
                    // Send the Supported PID request
                    SupportedPIDs transmissionPIDs;
                    if ( OBDModule->requestReceiveSupportedPIDs(
                             SID::CURRENT_STATS, OBDModule->mTransmissionECUSenderReceiver, transmissionPIDs ) )
                    {
                        std::sort( transmissionPIDs.begin(), transmissionPIDs.end() );
                        OBDModule->mSupportedPIDsTransmission.emplace( SID::CURRENT_STATS, transmissionPIDs );
                        std::ostringstream oss;
                        oss << ": ";
                        if ( !transmissionPIDs.empty() )
                        {
                            std::copy( transmissionPIDs.begin(),
                                       transmissionPIDs.end() - 1,
                                       std::ostream_iterator<int>( oss, "," ) );
                            oss << std::to_string( transmissionPIDs.back() );
                        }
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                  "Transmission ECU supports PIDs for SID " +
                                                      std::to_string( toUType( SID::CURRENT_STATS ) ) + oss.str() );
                        // Take the common PIDs between the ECU supported PIDs and the PIDs that are requested by
                        // Decoder Dictionary
                        OBDModule->updatePIDRequestList( SID::CURRENT_STATS,
                                                         ECUType::TRANSMISSION,
                                                         OBDModule->mSupportedPIDsTransmission,
                                                         OBDModule->mPIDsToRequestTransmission );
                    }
                    else
                    {
                        TraceModule::get().incrementVariable( TraceVariable::OBD_TRA_PID_REQ_ERROR );
                        OBDModule->mLogger.error( "OBDOverCANModule::doWork",
                                                  "Failed to request/receive Transmission ECU PIDs for SID: " +
                                                      std::to_string( toUType( SID::CURRENT_STATS ) ) );
                    }
                }

                // Request the PIDs ( up to 6 at a time )
                // To not overwhelm the bus, we split the PIDs into group of 6
                // and wait for the response.
                // Start with the ECM
                if ( OBDModule->getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) &&
                     !enginePIDs.empty() )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Requesting Emission PIDs from the ECM" );
                    if ( OBDModule->requestReceiveEmissionPIDs(
                             SID::CURRENT_STATS, enginePIDs, OBDModule->mEngineECUSenderReceiver, enginePIDInfo ) )
                    {
                        auto receptionTime = OBDModule->mClock->timeSinceEpochMs();
                        for ( auto const &signals : enginePIDInfo.mPIDsToValues )
                        {
                            // Note Signal buffer is a multi producer single consumer queue. Besides current thread,
                            // Vehicle Data Consumer will also push signals onto this buffer
                            TraceModule::get().incrementAtomicVariable(
                                TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                            if ( !OBDModule->mSignalBufferPtr->push(
                                     CollectedSignal( signals.first, receptionTime, signals.second ) ) )
                            {
                                TraceModule::get().decrementAtomicVariable(
                                    TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                                OBDModule->mLogger.warn( "OBDOverCANModule::doWork", "Signal Buffer full!" );
                            }
                            OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                      "Received Signal " + std::to_string( signals.first ) + " : " +
                                                          std::to_string( signals.second ) );
                        }
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                  "Received Emission PIDs data from the ECM" );
                    }
                    else
                    {
                        OBDModule->mLogger.warn( "OBDOverCANModule::doWork",
                                                 "Emission PIDs data from the ECM was not received" );
                    }
                }
                // TCM
                if ( OBDModule->mHasTransmission &&
                     OBDModule->getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) &&
                     !transmissionPIDs.empty() )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Requesting Emission PIDs from the TCM" );
                    if ( OBDModule->requestReceiveEmissionPIDs( SID::CURRENT_STATS,
                                                                transmissionPIDs,
                                                                OBDModule->mTransmissionECUSenderReceiver,
                                                                transmissionPIDInfo ) )
                    {
                        auto receptionTime = OBDModule->mClock->timeSinceEpochMs();
                        for ( auto const &signals : transmissionPIDInfo.mPIDsToValues )
                        {
                            // Note Signal buffer is a multi producer single consumer queue. Besides current thread,
                            // Vehicle Data Consumer will also push signals onto this buffer
                            TraceModule::get().incrementAtomicVariable(
                                TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                            if ( !OBDModule->mSignalBufferPtr->push(
                                     CollectedSignal( signals.first, receptionTime, signals.second ) ) )
                            {
                                TraceModule::get().decrementAtomicVariable(
                                    TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                                OBDModule->mLogger.warn( "OBDOverCANModule::doWork", "Signal Buffer full!" );
                            }
                        }
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                  "Received Emission PIDs data from the TCM" );
                    }
                    else
                    {
                        OBDModule->mLogger.warn( "OBDOverCANModule::doWork",
                                                 "Emission PIDs data from the TCM was not received" );
                    }
                }
                // Reschedule
                OBDModule->mPIDTimer.reset();
            }
        }
        // Execute the DTC flow if enabled
        if ( OBDModule->mShouldRequestDTCs.load( std::memory_order_relaxed ) )
        {
            bool successfulDTCRequest = false;
            DTCInfo dtcInfo;
            dtcInfo.receiveTime = OBDModule->mClock->timeSinceEpochMs();
            // Request then reschedule DTC requests stored DTCs from each ECU
            if ( OBDModule->mDTCRequestIntervalSeconds > 0 &&
                 OBDModule->mDTCTimer.getElapsedSeconds() >= OBDModule->mDTCRequestIntervalSeconds )
            {
                // ECM
                if ( OBDModule->requestReceiveDTCs( SID::STORED_DTC, OBDModule->mEngineECUSenderReceiver, dtcInfo ) )
                {
                    successfulDTCRequest = true;
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Received DTC data from the ECM" );
                }
                else
                {
                    OBDModule->mLogger.warn( "OBDOverCANModule::doWork", "Failed to receive DTCs from the ECM" );
                }
                // TCM
                if ( OBDModule->mHasTransmission )
                {
                    if ( OBDModule->requestReceiveDTCs(
                             SID::STORED_DTC, OBDModule->mTransmissionECUSenderReceiver, dtcInfo ) )
                    {
                        successfulDTCRequest = true;
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Received DTC data from the TCM" );
                    }
                    else
                    {
                        OBDModule->mLogger.warn( "OBDOverCANModule::doWork", "Failed to receive DTCs from the TCM" );
                    }
                }
                // Also DTCInfo strutcs without any DTCs must be pushed to the queue because it means
                // there was a OBD request that did not return any SID::STORED_DTCs
                if ( successfulDTCRequest )
                {
                    // Note DTC buffer is a single producer single consumer queue. This is the only
                    // thread to push DTC Info to the queue
                    if ( !OBDModule->mActiveDTCBufferPtr->push( dtcInfo ) )
                    {
                        OBDModule->mLogger.warn( "OBDOverCANModule::doWork", "DTC Buffer full!" );
                    }
                }

                // Reschedule
                OBDModule->mDTCTimer.reset();
            }
        }

        // Wait for the next cycle
        uint32_t sleepTime =
            OBDModule->mDTCRequestIntervalSeconds > 0
                ? std::min( OBDModule->mPIDRequestIntervalSeconds, OBDModule->mDTCRequestIntervalSeconds )
                : OBDModule->mPIDRequestIntervalSeconds;

        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                  " Waiting for :" + std::to_string( sleepTime ) + " seconds" );
        OBDModule->mWait.wait( static_cast<uint32_t>( sleepTime * 1000 ) );
    }
}

bool
OBDOverCANModule::requestReceiveEmissionPIDs( const SID &sid,
                                              const SupportedPIDs &pids,
                                              ISOTPOverCANSenderReceiver &isoTPSendReceive,
                                              EmissionInfo &info )
{
    size_t rangeCount = pids.size() / OBDOverCANModule::MAX_PID_RANGE;
    size_t rangeLeft = pids.size() % OBDOverCANModule::MAX_PID_RANGE;

    while ( rangeCount > 0 )
    {
        auto pidList =
            std::vector<PID>( pids.begin() + static_cast<uint32_t>( rangeCount - 1U ) * OBDOverCANModule::MAX_PID_RANGE,
                              pids.begin() + static_cast<uint32_t>( rangeCount ) * OBDOverCANModule::MAX_PID_RANGE );
        // start from the tail and walk backwards.
        if ( requestPIDs( sid, pidList, isoTPSendReceive ) )
        {
            if ( receivePIDs( sid, pidList, isoTPSendReceive, info ) )
            {
                mLogger.trace( "OBDOverCANModule::doWork",
                               "Received Emission PID data for SID: " + std::to_string( toUType( sid ) ) );
            }
            else
            {
                mLogger.warn( "OBDOverCANModule::doWork",
                              "Emission PID data for SID: " + std::to_string( toUType( sid ) ) + " Not received" );
            }
        }
        rangeCount--;
    }
    // request the remaining PIDs if any.
    if ( rangeLeft > 0 )
    {
        auto pidList = std::vector<PID>( pids.end() - static_cast<uint32_t>( rangeLeft ), pids.end() );
        if ( requestPIDs( sid, pidList, isoTPSendReceive ) )
        {
            if ( receivePIDs( sid, pidList, isoTPSendReceive, info ) )
            {
                mLogger.trace( "OBDOverCANModule::doWork",
                               "Received Emission PID data for SID: " + std::to_string( toUType( sid ) ) );
            }
            else
            {
                mLogger.warn( "OBDOverCANModule::doWork",
                              "Emission PID data for SID: " + std::to_string( toUType( sid ) ) + " Not received" );
            }
        }
    }

    return !info.mPIDsToValues.empty();
}

bool
OBDOverCANModule::requestReceiveSupportedPIDs( const SID &sid,
                                               ISOTPOverCANSenderReceiver &isoTPSendReceive,
                                               SupportedPIDs &supportedPIDs )
{
    // Function will return true if it receive supported PIDs from the ECU
    bool requestStatus = false;
    static_assert( supportedPIDRange.size() <= 8,
                   "Array length for supported PID range shall be less or equal than 8" );
    mLogger.trace( "OBDOverCANModule::requestReceiveSupportedPIDs", "send supported PID requests" );
    supportedPIDs.clear();
    // Request supported PID range. Per ISO 15765, we can only send six PID at one time
    auto pidList = std::vector<PID>( supportedPIDRange.begin(),
                                     supportedPIDRange.begin() +
                                         std::min( OBDOverCANModule::MAX_PID_RANGE, supportedPIDRange.size() ) );
    if ( requestPIDs( sid, pidList, isoTPSendReceive ) )
    {
        // Wait and process the response
        if ( receiveSupportedPIDs( sid, isoTPSendReceive, supportedPIDs ) )
        {
            // we have received a list of supported PIDs
            requestStatus = true;
        }
        else
        {
            // log warning as all emissions-related OBD ECUs which support at least one of the
            // services defined in J1979 shall support Service $01 and PID $00
            mLogger.warn( "OBDOverCANModule::requestReceiveSupportedPIDs", "Fail to receive supported PID range" );
        }
    }
    // check if we need to send out more PID range request
    if ( OBDOverCANModule::MAX_PID_RANGE < supportedPIDRange.size() )
    {
        pidList =
            std::vector<PID>( supportedPIDRange.begin() + OBDOverCANModule::MAX_PID_RANGE, supportedPIDRange.end() );
        if ( requestPIDs( sid, pidList, isoTPSendReceive ) )
        {
            // Wait and process the response
            if ( receiveSupportedPIDs( sid, isoTPSendReceive, supportedPIDs ) )
            {
                // we have received a list of supported PIDs
                requestStatus = true;
            }
        }
    }
    return requestStatus;
}

bool
OBDOverCANModule::connect()
{
    if ( mHasTransmission )
    {
        mTransmissionECUSenderReceiver.connect();
    }

    return mEngineECUSenderReceiver.connect() && start();
}

bool
OBDOverCANModule::disconnect()
{
    if ( mHasTransmission )
    {
        mTransmissionECUSenderReceiver.disconnect();
    }
    return mEngineECUSenderReceiver.disconnect() && stop();
}

bool
OBDOverCANModule::isAlive()
{
    if ( mHasTransmission )
    {
        return mThread.isValid() && mThread.isActive() && mEngineECUSenderReceiver.isAlive() &&
               mTransmissionECUSenderReceiver.isAlive();
    }
    else
    {
        return mThread.isValid() && mThread.isActive() && mEngineECUSenderReceiver.isAlive();
    }
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
                // Need to sort the vector to make it easier to search for common PIDs between this vector
                // and the PIDs supported by ECU.
                std::sort( pidsRequestedByDecoderDict.begin(), pidsRequestedByDecoderDict.end() );
                std::ostringstream oss;
                if ( !pidsRequestedByDecoderDict.empty() )
                {
                    std::copy( pidsRequestedByDecoderDict.begin(),
                               pidsRequestedByDecoderDict.end() - 1,
                               std::ostream_iterator<int>( oss, "," ) );
                    oss << std::to_string( pidsRequestedByDecoderDict.back() );
                }
                mLogger.trace( "OBDOverCANModule::onChangeOfActiveDictionary",
                               "Decoder Dictionary requests PIDs: " + oss.str() );
                // For now we only support OBD Service Mode 1 PID
                mPIDsRequestedByDecoderDict[SID::CURRENT_STATS] = pidsRequestedByDecoderDict;
                // If the program already know the supported PIDs from ECU, below two update will update
                // the PIDs list to request from ECU. Otherwise, the two function below will not perform anything.
                updatePIDRequestList( SID::CURRENT_STATS, ECUType::ENGINE, mSupportedPIDsEngine, mPIDsToRequestEngine );
                updatePIDRequestList(
                    SID::CURRENT_STATS, ECUType::TRANSMISSION, mSupportedPIDsTransmission, mPIDsToRequestTransmission );
                // Pass on the decoder manifest to the OBD Decoder and wake up the thread.
                // Before that we should interrupt the thread so that no further decoding
                // is done using the previous decoder, then assign the new decoder manifest,
                // the wake up the thread.
                if ( !mDecoderDictionaryPtr->empty() )
                {
                    mDecoderManifestAvailable.store( true, std::memory_order_relaxed );
                    // Wake up the worker thread.
                    mDataAvailableWait.notify();
                    mLogger.info( "OBDOverCANModule::onChangeOfActiveDictionary", "Decoder Manifest Updated" );
                }
            }
            else
            {
                mLogger.warn( "OBDOverCANModule::onChangeOfActiveDictionary",
                              "Received Invalid Decoder Manifest, ignoring it" );
            }
        }
    }
}

void
OBDOverCANModule::updatePIDRequestList( const SID &sid,
                                        const ECUType &type,
                                        std::map<SID, SupportedPIDs> &supportedPIDs,
                                        std::map<SID, std::vector<PID>> &pidsToRequestPerService )
{
    // Update the PID Request List with PIDs that are common between decoder dictionary and the PIDs supported by ECU
    if ( supportedPIDs.find( sid ) != supportedPIDs.end() &&
         mPIDsRequestedByDecoderDict.find( sid ) != mPIDsRequestedByDecoderDict.end() )
    {
        std::vector<PID> pidsToRequest{};
        // Note that the two vector has to be sorted previously to use the function below properly
        std::set_intersection( supportedPIDs[sid].begin(),
                               supportedPIDs[sid].end(),
                               mPIDsRequestedByDecoderDict[sid].begin(),
                               mPIDsRequestedByDecoderDict[sid].end(),
                               std::back_inserter( pidsToRequest ) );
        std::ostringstream oss;
        oss << "The PIDs to Request from ";
        switch ( type )
        {
        case ECUType::ENGINE:
            oss << "Engine ECU are: ";
            break;
        case ECUType::TRANSMISSION:
            oss << "Transmission ECU are: ";
            break;
        default:
            oss << "ECU are: ";
        }
        if ( !pidsToRequest.empty() )
        {
            std::copy( pidsToRequest.begin(), pidsToRequest.end() - 1, std::ostream_iterator<int>( oss, "," ) );
            oss << std::to_string( pidsToRequest.back() );
        }
        mLogger.trace( "OBDOverCANModule::updatePIDRequestList", oss.str() );
        pidsToRequestPerService[sid] = pidsToRequest;
    }
}

bool
OBDOverCANModule::receiveSupportedPIDs( const SID &sid,
                                        ISOTPOverCANSenderReceiver &isoTPSendReceive,
                                        SupportedPIDs &supportedPIDs )
{
    std::vector<uint8_t> ecuResponse;
    // Receive the PDU that has the Supported PIDs for this SID
    // decoded according to J1979 8.1.2.2
    if ( !isoTPSendReceive.receivePDU( ecuResponse ) )
    {
        return false;
    }
    if ( !ecuResponse.empty() )
    {
        std::ostringstream oss;
        std::copy( ecuResponse.begin(), ecuResponse.end() - 1, std::ostream_iterator<int>( oss, "," ) );
        oss << std::to_string( ecuResponse.back() );
        mLogger.trace( "OBDOverCANModule::receiveSupportedPIDs", "ECU Response: " + oss.str() );
        return mOBDDataDecoder->decodeSupportedPIDs( sid, ecuResponse, supportedPIDs );
    }
    return false;
}

bool
OBDOverCANModule::requestPIDs( const SID &sid,
                               const std::vector<PID> &pids,
                               ISOTPOverCANSenderReceiver &isoTPSendReceive )
{
    mTxPDU.clear();
    // Assume that the PIDs belong to the SID, and that they are
    // supported by the ECU
    // ECUs do not support more than 6 PIDs at a time.
    if ( pids.size() > OBDOverCANModule::MAX_PID_RANGE )
    {
        return false;
    }
    // First insert the SID
    mTxPDU.emplace_back( static_cast<uint8_t>( sid ) );
    // Then insert the items of the PIDs
    mTxPDU.insert( std::end( mTxPDU ), std::begin( pids ), std::end( pids ) );
    std::ostringstream oss;
    std::copy( mTxPDU.begin(), mTxPDU.end() - 1, std::ostream_iterator<int>( oss, "," ) );
    oss << std::to_string( mTxPDU.back() );
    mLogger.trace( "OBDOverCANModule::requestPIDs", "Transmit PDU: " + oss.str() );
    // Send
    return isoTPSendReceive.sendPDU( mTxPDU );
}

bool
OBDOverCANModule::receivePIDs( const SID &sid,
                               const std::vector<PID> &pids,
                               ISOTPOverCANSenderReceiver &isoTPSendReceive,
                               EmissionInfo &info )
{
    std::vector<uint8_t> ecuResponse;
    // Receive the PDU that has the Supported PIDs for this SID
    // decoded according to J1979 8.1.2.2
    if ( !isoTPSendReceive.receivePDU( ecuResponse ) )
    {
        return false;
    }
    // The info structure will be appended with the new decoded PIDs
    if ( !ecuResponse.empty() )
    {
        std::ostringstream oss;
        std::copy( ecuResponse.begin(), ecuResponse.end() - 1, std::ostream_iterator<int>( oss, "," ) );
        oss << std::to_string( ecuResponse.back() );
        mLogger.trace( "OBDOverCANModule::receivePIDs", "ECU Response: " + oss.str() );
        return mOBDDataDecoder->decodeEmissionPIDs( sid, pids, ecuResponse, info );
    }

    return false;
}

bool
OBDOverCANModule::requestReceiveVIN( std::string &vin )
{
    mTxPDU.clear();
    mTxPDU = { static_cast<uint8_t>( vehicleIdentificationNumberRequest.mSID ),
               static_cast<uint8_t>( vehicleIdentificationNumberRequest.mPID ) };
    // Send
    if ( mEngineECUSenderReceiver.sendPDU( mTxPDU ) )
    {
        std::vector<uint8_t> ecuResponse;
        // Receive the PDU and extract the VIN
        if ( mEngineECUSenderReceiver.receivePDU( ecuResponse ) && !ecuResponse.empty() &&
             mOBDDataDecoder->decodeVIN( ecuResponse, vin ) )
        {
            return true;
        }
    }
    return false;
}

bool
OBDOverCANModule::requestDTCs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive )
{
    mTxPDU.clear();
    // Only SID is required for DTC requests
    mTxPDU.emplace_back( static_cast<uint8_t>( sid ) );
    // Send
    return isoTPSendReceive.sendPDU( mTxPDU );
}

bool
OBDOverCANModule::receiveDTCs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive, DTCInfo &info )
{
    std::vector<uint8_t> ecuResponse;
    if ( !isoTPSendReceive.receivePDU( ecuResponse ) )
    {
        return false;
    }
    // The info structure will be appended with the new decoded DTCs
    if ( !ecuResponse.empty() && mOBDDataDecoder->decodeDTCs( sid, ecuResponse, info ) )
    {
        return true;
    }

    return false;
}

bool
OBDOverCANModule::requestReceiveDTCs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive, DTCInfo &info )
{
    // Request and try to receive within the time interval
    if ( requestDTCs( sid, isoTPSendReceive ) )
    {
        // Wait and process the response
        if ( receiveDTCs( sid, isoTPSendReceive, info ) )
        {
            return true;
        }
    }
    return false;
}

bool
OBDOverCANModule::getPIDsToRequest( const SID &sid, const ECUType &type, SupportedPIDs &supportedPIDs ) const
{
    if ( type == ECUType::ENGINE )
    {
        auto pidIterator = mPIDsToRequestEngine.find( sid );
        if ( pidIterator == mPIDsToRequestEngine.end() )
        {
            return false;
        }
        supportedPIDs = pidIterator->second;
        return true;
    }
    else if ( type == ECUType::TRANSMISSION )
    {
        auto pidIterator = mPIDsToRequestTransmission.find( sid );
        if ( pidIterator == mPIDsToRequestTransmission.end() )
        {
            return false;
        }
        supportedPIDs = pidIterator->second;
        return true;
    }

    return false;
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
