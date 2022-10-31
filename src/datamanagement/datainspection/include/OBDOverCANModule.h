// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "IActiveConditionProcessor.h"
#include "IActiveDecoderDictionaryListener.h"
#include "LoggingModule.h"
#include "OBDDataDecoder.h"
#include "OBDDataTypes.h"
#include "OBDOverCANSessionManager.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "businterfaces/ISOTPOverCANSenderReceiver.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::DataManagement;

/**
 * @brief This module handles the collection of Emission related data and DTC codes
 * over the OBD Stack. It manages the OBD Keep Alive Session manager, and issues
 * regularly OBD Requests to the ECU Network. It notifies the Engine thread if
 * certain events are detected as defined in the Collection Scheme.
 * If no event trigger condition is met, the data collected is put in a circular buffer.
 */
class OBDOverCANModule : public IActiveDecoderDictionaryListener, public IActiveConditionProcessor
{
public:
    OBDOverCANModule();
    ~OBDOverCANModule() override;

    OBDOverCANModule( const OBDOverCANModule & ) = delete;
    OBDOverCANModule &operator=( const OBDOverCANModule & ) = delete;
    OBDOverCANModule( OBDOverCANModule && ) = delete;
    OBDOverCANModule &operator=( OBDOverCANModule && ) = delete;

    /**
     * @brief Initializes the OBD Diagnostic Session with Engine and TX ECUs.
     * @param signalBufferPtr Signal Buffer shared pointer.
     * @param activeDTCBufferPtr Active DTC buffer shared pointer
     * @param gatewayCanInterfaceName CAN IF Name where the OBD stack on the ECU
     * is running. Typically on the Gateway ECU.
     * @param pidRequestIntervalSeconds Interval in seconds used to schedule PID requests
     * @param dtcRequestIntervalSeconds Interval in seconds used to schedule DTC requests
     * @param useExtendedIDs use Extended CAN IDs on TX and RX side.
     * @param hasTransmissionECU specifies whether the vehicle has a Transmission ECU
     * @return True if successful. False if both pidRequestIntervalSeconds
     * and dtcRequestIntervalSeconds are zero i.e. no collection
     *
     */
    bool init( SignalBufferPtr signalBufferPtr,
               ActiveDTCBufferPtr activeDTCBufferPtr,
               const std::string &gatewayCanInterfaceName,
               const uint32_t &pidRequestIntervalSeconds,
               const uint32_t &dtcRequestIntervalSeconds = 0,
               const bool &useExtendedIDs = false,
               const bool &hasTransmissionECU = false );

    /**
     * @brief Creates an ISO-TP connection to the Engine/Transmission ECUs. Starts the
     * Keep Alive cyclic thread.
     * @return True if successful. False otherwise.
     */
    bool connect();

    /**
     * @brief Closes the ISO-TP connection to the Engine/Transmission ECUs. Stops the
     * Keep Alive cyclic tread.
     * @return True if successful. False otherwise.
     */
    bool disconnect();

    /**
     * @brief Returns the health state of the cyclic thread and the
     * ISO-TP Connection.
     * @return True if successful. False otherwise.
     */
    bool isAlive();

    // From IActiveDecoderDictionaryListener
    // We need this to know whether PIDs should be requested or not
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

    // From IActiveConditionProcessor
    // We need this to know whether DTCs should be requested or not
    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &activeConditions ) override;

    /**
     * @brief Returns the PIDs that are to be requested from ECU
     * @param sid the SID e.g. Mode 1
     * @param supportedPIDs container where the result will be copied
     * @param type the ECU Type e.g. Engine , Transmission
     * @return True if successful. False if the SID was not processed.
     */
    bool getPIDsToRequest( const SID &sid, const ECUType &type, SupportedPIDs &supportedPIDs ) const;

    /**
     * @brief Returns the VIN received in this OBD Session
     * @param[out] vin output string
     * @return True if VIN has been received, false otherwise
     */
    inline bool
    getVIN( std::string &vin ) const
    {
        vin = mVIN;
        return !vin.empty();
    }

    /**
     * @brief Handle of the Signal Output Buffer. This buffer shared between Collection Engine
     * Vehicle Data Consumer and OBDOverCANModule
     * @return shared object pointer to the Signal buffer.
     */
    inline SignalBufferPtr
    getSignalBufferPtr() const
    {
        return mSignalBufferPtr;
    }

    /**
     * @brief Handle of the DTC Output Buffer. This buffer shared between Collection Engine
     * and OBDOverCANModule.
     * @return shared object pointer to the DTC buffer.
     */
    inline ActiveDTCBufferPtr
    getActiveDTCBufferPtr() const
    {
        return mActiveDTCBufferPtr;
    }

private:
    // Start the  thread
    bool start();
    // Stop the  thread
    bool stop();
    // Intercepts stop signals.
    bool shouldStop() const;
    // Main worker function. The following operations are coded by the function
    // 1- Sends  Supported PIDs request to Engine and TX ECUs
    // 2- Stores the supported PIDs
    // Cyclically:
    // 3- Send  PID requests ( up to 6 at a time )
    // 4- waits for the response from the ECU
    // 5- If an RX PDU arrives, decodes the value and puts the result to the
    // output buffer
    static void doWork( void *data );

    // Receives the supported PID request to the specific ECU and SID
    bool receiveSupportedPIDs( const SID &sid,
                               ISOTPOverCANSenderReceiver &isoTPSendReceive,
                               SupportedPIDs &supportedPIDs );
    // Update the PID Request List with PIDs that are common between decoder dictionary and the PIDs supported by ECU
    void updatePIDRequestList( const SID &sid,
                               const ECUType &type,
                               std::map<SID, SupportedPIDs> &supportedPIDs,
                               std::map<SID, std::vector<PID>> &pidsToRequestPerService );
    // Request a set of PIDs from one ECU ( up to 6 PIDs at a time )
    bool requestPIDs( const SID &sid, const std::vector<PID> &pids, ISOTPOverCANSenderReceiver &isoTPSendReceive );
    bool receivePIDs( const SID &sid,
                      const std::vector<PID> &pids,
                      ISOTPOverCANSenderReceiver &isoTPSendReceive,
                      EmissionInfo &info );

    bool requestReceiveSupportedPIDs( const SID &sid,
                                      ISOTPOverCANSenderReceiver &isoTPSendReceive,
                                      SupportedPIDs &supportedPIDs );

    bool requestReceiveEmissionPIDs( const SID &sid,
                                     const SupportedPIDs &pids,
                                     ISOTPOverCANSenderReceiver &isoTPSendReceive,
                                     EmissionInfo &info );

    // For SID 9/pid 0x02
    bool requestReceiveVIN( std::string &vin );
    // For SID 3 and 7
    bool requestDTCs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive );
    bool receiveDTCs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive, DTCInfo &info );
    bool requestReceiveDTCs( const SID &sid, ISOTPOverCANSenderReceiver &isoTPSendReceive, DTCInfo &info );

    static constexpr size_t MAX_PID_RANGE = 6U;
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mDecoderManifestAvailable{ false };
    std::atomic<bool> mShouldRequestDTCs{ false };
    mutable std::mutex mThreadMutex;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    ISOTPOverCANSenderReceiver mEngineECUSenderReceiver;
    ISOTPOverCANSenderReceiver mTransmissionECUSenderReceiver;
    // Stop signal
    Platform::Linux::Signal mWait;
    // Decoder Manifest and campaigns availability Signal
    Platform::Linux::Signal mDataAvailableWait;
    std::vector<uint8_t> mTxPDU;
    std::vector<uint8_t> mRxPDU;
    // Signal Buffer shared pointer
    SignalBufferPtr mSignalBufferPtr;
    // Active DTC Buffer shared pointer
    ActiveDTCBufferPtr mActiveDTCBufferPtr;
    uint32_t mPIDRequestIntervalSeconds;
    uint32_t mDTCRequestIntervalSeconds;
    uint32_t mOBDHeartBeatIntervalSeconds;
    bool mHasTransmission;
    std::string mVIN;
    Timer mTimer;
    Timer mDTCTimer;
    Timer mPIDTimer;
    // Supported PIDs for both ECUs
    std::map<SID, SupportedPIDs> mSupportedPIDsEngine;
    std::map<SID, SupportedPIDs> mSupportedPIDsTransmission;
    // PIDs that are required by decoder dictionary
    std::unordered_map<SID, std::vector<PID>> mPIDsRequestedByDecoderDict;
    // The PIDs to request from ECUs. It will be the common PIDs that are required by
    // decoder dictionary as well as supported by ECU
    std::map<SID, std::vector<PID>> mPIDsToRequestEngine;
    std::map<SID, std::vector<PID>> mPIDsToRequestTransmission;

    // OBD Session Manager
    // Will not be used. This is for Future use for ECU discovery.
    // OBDOverCANSessionManager mSessionManager;
    std::unique_ptr<OBDDataDecoder> mOBDDataDecoder;
    // Mutex to ensure atomic decoder dictionary shared pointer content assignment
    std::mutex mDecoderDictMutex;
    // shared pointer to decoder dictionary
    std::shared_ptr<OBDDecoderDictionary> mDecoderDictionaryPtr;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
