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

// Includes
#include "OBDOverCANModule.h"
#include "OBDDataTypes.h"
#include "TraceModule.h"
#include <bitset>
#include <iostream>
#include <iterator>
#include <string.h>
#define MAX_PID_RANGE ( 6U )
namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

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
        optionsEngine.mSourceCANId = ENGINE_ECU_TX_EXTENDED;
        optionsEngine.mDestinationCANId = ENGINE_ECU_RX_EXTENDED;
        // TCM
        optionsTransmission.mIsExtendedId = true;
        optionsTransmission.mSourceCANId = TRANSMISSION_ECU_TX_EXTENDED;
        optionsTransmission.mDestinationCANId = TRANSMISSION_ECU_RX_EXTENDED;
    }
    else
    {
        // ECM
        optionsEngine.mSourceCANId = ENGINE_ECU_TX;
        optionsEngine.mDestinationCANId = ENGINE_ECU_RX;
        // TCM
        optionsTransmission.mSourceCANId = TRANSMISSION_ECU_TX;
        optionsTransmission.mDestinationCANId = TRANSMISSION_ECU_RX;
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
    mOBDDataDecoder.reset( new OBDDataDecoder() );

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
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
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

    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
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
            OBDModule->mDataAvailableWait.wait( Platform::Signal::WaitWithPredicate );
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
            engineDiagnosticInfo.mEcuType = ENGINE;
            transmissionDiagnosticInfo.mEcuType = TRANSMISSION;
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
                             CURRENT_STATS, OBDModule->mEngineECUSenderReceiver, enginePIDs ) )
                    {
                        OBDModule->mSupportedPIDsEngine.emplace( CURRENT_STATS, enginePIDs );
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                  "Received Engine ECU PIDs for SID: " +
                                                      std::to_string( CURRENT_STATS ) );
                    }
                    else
                    {
                        TraceModule::get().incrementVariable( TraceVariable::OBD_ENG_PID_REQ_ERROR );
                        OBDModule->mLogger.error( "OBDOverCANModule::doWork",
                                                  "Failed to request/receive Engine ECU PIDs for SID: " +
                                                      std::to_string( CURRENT_STATS ) );
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
                             CURRENT_STATS, OBDModule->mTransmissionECUSenderReceiver, transmissionPIDs ) )
                    {
                        OBDModule->mSupportedPIDsTransmission.emplace( CURRENT_STATS, transmissionPIDs );
                        OBDModule->mLogger.trace( "OBDOverCANModule::doWork",
                                                  "Received Transmission ECU PIDs for SID: " +
                                                      std::to_string( CURRENT_STATS ) );
                    }
                    else
                    {
                        TraceModule::get().incrementVariable( TraceVariable::OBD_TRA_PID_REQ_ERROR );
                        OBDModule->mLogger.error( "OBDOverCANModule::doWork",
                                                  "Failed to request/receive Transmission ECU PIDs for SID: " +
                                                      std::to_string( CURRENT_STATS ) );
                    }
                }

                // Request the PIDs ( up to 6 at a time )
                // To not overwhelm the bus, we split the PIDs into group of 6
                // and wait for the response.
                // Start with the ECM
                if ( OBDModule->getSupportedPIDs( CURRENT_STATS, ENGINE, enginePIDs ) && !enginePIDs.empty() )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Requesting Emission PIDs from the ECM" );
                    if ( OBDModule->requestReceiveEmissionPIDs(
                             CURRENT_STATS, enginePIDs, OBDModule->mEngineECUSenderReceiver, enginePIDInfo ) )
                    {
                        auto receptionTime = OBDModule->mClock->timeSinceEpochMs();
                        for ( auto const &signals : enginePIDInfo.mPIDsToValues )
                        {
                            // Note Signal buffer is a multi producer single consumer queue. Besides current thread,
                            // Network Channel Consumer will also push signals onto this buffer
                            TraceModule::get().incrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                            if ( !OBDModule->mSignalBufferPtr->push(
                                     CollectedSignal( signals.first, receptionTime, signals.second ) ) )
                            {
                                TraceModule::get().decrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
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
                     OBDModule->getSupportedPIDs( CURRENT_STATS, TRANSMISSION, transmissionPIDs ) &&
                     !transmissionPIDs.empty() )
                {
                    OBDModule->mLogger.trace( "OBDOverCANModule::doWork", "Requesting Emission PIDs from the TCM" );
                    if ( OBDModule->requestReceiveEmissionPIDs( CURRENT_STATS,
                                                                transmissionPIDs,
                                                                OBDModule->mTransmissionECUSenderReceiver,
                                                                transmissionPIDInfo ) )
                    {
                        auto receptionTime = OBDModule->mClock->timeSinceEpochMs();
                        for ( auto const &signals : transmissionPIDInfo.mPIDsToValues )
                        {
                            // Note Signal buffer is a multi producer single consumer queue. Besides current thread,
                            // Network Channel Consumer will also push signals onto this buffer
                            TraceModule::get().incrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                            if ( !OBDModule->mSignalBufferPtr->push(
                                     CollectedSignal( signals.first, receptionTime, signals.second ) ) )
                            {
                                TraceModule::get().decrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
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
                if ( OBDModule->requestReceiveDTCs( STORED_DTC, OBDModule->mEngineECUSenderReceiver, dtcInfo ) )
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
                             STORED_DTC, OBDModule->mTransmissionECUSenderReceiver, dtcInfo ) )
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
                // there was a OBD request that did not return any STORED_DTCs
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
    size_t rangeCount = pids.size() / MAX_PID_RANGE;
    size_t rangeLeft = pids.size() % MAX_PID_RANGE;

    while ( rangeCount > 0 )
    {
        // start from the tail and walk backwards.
        if ( requestPIDs( sid,
                          std::vector<PID>( pids.begin() + static_cast<uint32_t>( rangeCount - 1U ) * MAX_PID_RANGE,
                                            pids.begin() + static_cast<uint32_t>( rangeCount ) * MAX_PID_RANGE ),
                          isoTPSendReceive ) )
        {
            if ( receivePIDs( sid, isoTPSendReceive, info ) )
            {
                mLogger.trace( "OBDOverCANModule::doWork",
                               "Received Emission PID data for SID: " + std::to_string( sid ) );
            }
            else
            {
                mLogger.warn( "OBDOverCANModule::doWork",
                              "Emission PID data for SID: " + std::to_string( sid ) + " Not received" );
            }
        }
        rangeCount--;
    }
    // request the remaining PIDs if any.
    if ( rangeLeft > 0 )
    {
        if ( requestPIDs( sid,
                          std::vector<PID>( pids.end() - static_cast<uint32_t>( rangeLeft ), pids.end() ),
                          isoTPSendReceive ) )
        {
            if ( receivePIDs( sid, isoTPSendReceive, info ) )
            {
                mLogger.trace( "OBDOverCANModule::doWork",
                               "Received Emission PID data for SID: " + std::to_string( sid ) );
            }
            else
            {
                mLogger.warn( "OBDOverCANModule::doWork",
                              "Emission PID data for SID: " + std::to_string( sid ) + " Not received" );
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

    // Request and try to receive within the time interval
    if ( requestSupportedPIDs( sid, isoTPSendReceive ) )
    {
        // Wait and process the response
        if ( receiveSupportedPIDs( sid, isoTPSendReceive, supportedPIDs ) )
        {
            return true;
        }
    }
    return false;
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
                                              NetworkChannelProtocol networkProtocol )
{
    if ( networkProtocol == OBD )
    {
        {

            std::lock_guard<std::mutex> lock( mDecoderDictMutex );
            mDecoderDictionaryPtr = std::make_shared<OBDDecoderDictionary>();
            // As OBD only has one port, we expect the decoder dictionary only has one channel
            if ( mOBDDataDecoder != nullptr && dictionary != nullptr &&
                 dictionary->canMessageDecoderMethod.size() == 1 )
            {
                // Iterate through the received generic decoder dictionary to construct the OBD specific dictionary
                for ( const auto &canMessageDecoderMethod : dictionary->canMessageDecoderMethod.cbegin()->second )
                {
                    // The key is PID; The Value is decoder format
                    mDecoderDictionaryPtr->emplace( canMessageDecoderMethod.first,
                                                    canMessageDecoderMethod.second.format );
                }
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

bool
OBDOverCANModule::requestSupportedPIDs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive )
{
    mTxPDU.clear();
    // Every ECU should support such kind of request.
    // J1979 8.1
    // First insert the SID
    mTxPDU.emplace_back( static_cast<uint8_t>( sid ) );
    // Then insert the PID ranges
    mTxPDU.insert( mTxPDU.end(), std::begin( supportedPIDRange ), std::end( supportedPIDRange ) );

    return isoTPSendReceive.sendPDU( mTxPDU );
}

bool
OBDOverCANModule::receiveSupportedPIDs( const SID &sid,
                                        ISOTPOverCANSenderReceiver &isoTPSendReceive,
                                        SupportedPIDs &supportedPIDs )
{
    supportedPIDs.clear();
    std::vector<uint8_t> ecuResponse;
    // Receive the PDU that has the Supported PIDs for this SID
    // decoded according to J1979 8.1.2.2
    if ( !isoTPSendReceive.receivePDU( ecuResponse ) )
    {
        return false;
    }
    if ( !ecuResponse.empty() && mOBDDataDecoder->decodeSupportedPIDs( sid, ecuResponse, supportedPIDs ) )
    {
        return true;
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
    if ( pids.size() > MAX_PID_RANGE )
    {
        return false;
    }
    // First insert the SID
    mTxPDU.emplace_back( static_cast<uint8_t>( sid ) );
    // Then insert the items of the PIDs
    mTxPDU.insert( std::end( mTxPDU ), std::begin( pids ), std::end( pids ) );
    // Send
    return isoTPSendReceive.sendPDU( mTxPDU );
}

bool
OBDOverCANModule::receivePIDs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive, EmissionInfo &info )
{
    std::vector<uint8_t> ecuResponse;
    // Receive the PDU that has the Supported PIDs for this SID
    // decoded according to J1979 8.1.2.2
    if ( !isoTPSendReceive.receivePDU( ecuResponse ) )
    {
        return false;
    }
    // The info structure will be appended with the new decoded PIDs
    if ( !ecuResponse.empty() && mOBDDataDecoder->decodeEmissionPIDs( sid, ecuResponse, info ) )
    {
        return true;
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
OBDOverCANModule::getSupportedPIDs( const SID &sid, const ECUType &type, SupportedPIDs &supportedPIDs ) const
{
    if ( type == ECUType::ENGINE )
    {
        auto pidIterator = mSupportedPIDsEngine.find( sid );
        if ( pidIterator == mSupportedPIDsEngine.end() )
        {
            return false;
        }
        supportedPIDs = pidIterator->second;
        return true;
    }
    else if ( type == ECUType::TRANSMISSION )
    {
        auto pidIterator = mSupportedPIDsTransmission.find( sid );
        if ( pidIterator == mSupportedPIDsTransmission.end() )
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
