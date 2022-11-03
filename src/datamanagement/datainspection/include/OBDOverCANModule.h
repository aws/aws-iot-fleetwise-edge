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
#include "OBDOverCANECU.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <linux/can/raw.h>
#include <sstream>

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
 * @brief This class is responsible for coordinating OBD requests on all ECUs
 */

class OBDOverCANModule : public IActiveDecoderDictionaryListener, public IActiveConditionProcessor
{
public:
    OBDOverCANModule() = default;
    ~OBDOverCANModule() override;

    OBDOverCANModule( const OBDOverCANModule & ) = delete;
    OBDOverCANModule &operator=( const OBDOverCANModule & ) = delete;
    OBDOverCANModule( OBDOverCANModule && ) = delete;
    OBDOverCANModule &operator=( OBDOverCANModule && ) = delete;

    /**
     * @brief Initializes the OBD Diagnostic Session.
     * @param signalBufferPtr Signal Buffer shared pointer.
     * @param activeDTCBufferPtr Active DTC buffer shared pointer
     * @param gatewayCanInterfaceName CAN IF Name where the OBD stack on the ECU
     * is running. Typically on the Gateway ECU.
     * @param pidRequestIntervalSeconds Interval in seconds used to schedule PID requests
     * @param dtcRequestIntervalSeconds Interval in seconds used to schedule DTC requests
     * @return True if successful. False if both pidRequestIntervalSeconds
     * and dtcRequestIntervalSeconds are zero i.e. no collection
     */
    bool init( SignalBufferPtr signalBufferPtr,
               ActiveDTCBufferPtr activeDTCBufferPtr,
               const std::string &gatewayCanInterfaceName,
               const uint32_t &pidRequestIntervalSeconds,
               const uint32_t &dtcRequestIntervalSeconds = 0 );
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
     * @brief Returns the health state of the cyclic thread
     * @return True if successful. False otherwise.
     */
    bool isAlive();

    /**
     * @brief A callback function to be invoked when there's a new Decoder Dictionary. If the networkProtocol
     * is OBD, this module will update the decoder dictionary.
     *
     * @param dictionary decoder dictionary
     * @param networkProtocol network protocol which can be OBD
     */
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

    /**
     * @brief A callback function to be invoked when there's a new Inspection Matrix. The inspection
     * matrix will specify whether or not we shall collect DTC
     *
     * @param activeConditions Inspection Matrix
     */
    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &activeConditions ) override;

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
    /**
     * @brief Automatically detect all ECUs on a vehicle by sending broadcast request.
     * A supported PIDs broadcast request is sent.
     * @param isExtendedID If isExendedID is true, send a 29-bit broadcast. Otherwise, send a 11-bit broadcast
     * @param canIDResponses Vector saving responses from auto detect process
     * @return True if no errors occurs. False otherwise.
     */
    bool autoDetectECUs( bool isExtendedID, std::vector<uint32_t> &canIDResponses );

    // calculate ECU CAN Transmit ID from Receiver ID
    static constexpr uint32_t getTxIDByRxID( uint32_t rxId );

    /**
     * @brief Initialize ECUs by detected CAN IDs
     * @param canIDResponse Detected CAN ID via broadcast request
     * @return True if all OBDOverCANECU modules are initialized successfully for ECUs
     */
    bool initECUs( std::vector<uint32_t> &canIDResponse );

    // Start the  thread
    bool start();
    // Stop the  thread
    bool stop();
    // Intercepts stop signals.
    bool shouldStop() const;
    // Main worker function. The following operations are coded by the function
    // 1- Sends  Supported PIDs request to detected ECUs
    // 2- Stores the supported PIDs
    // Cyclically:
    // 3- Send  PID requests ( up to 6 at a time )
    // 4- waits for the response from the ECU
    // 5- If an RX PDU arrives, decodes the value and puts the result to the
    // output buffer
    static void doWork( void *data );

    // This function will assign PIDs to each ECU
    bool assignPIDsToECUs();

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mDecoderManifestAvailable{ false };
    std::atomic<bool> mShouldRequestDTCs{ false };
    std::vector<std::shared_ptr<OBDOverCANECU>> mECUs;
    mutable std::mutex mThreadMutex;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    // Stop signal
    Platform::Linux::Signal mWait;
    // Decoder Manifest and campaigns availability Signal
    Platform::Linux::Signal mDataAvailableWait;

    // Signal Buffer shared pointer
    SignalBufferPtr mSignalBufferPtr;
    // Active DTC Buffer shared pointer
    ActiveDTCBufferPtr mActiveDTCBufferPtr;
    uint32_t mPIDRequestIntervalSeconds{ 0 };
    uint32_t mDTCRequestIntervalSeconds{ 0 };
    std::string mGatewayCanInterfaceName;
    Timer mDTCTimer;
    Timer mPIDTimer;

    // This set contains PIDs that have been assigned to ECUs
    std::unordered_set<PID> mPIDAssigned;

    // PIDs that are required by decoder dictionary
    std::unordered_map<SID, std::vector<PID>> mPIDsRequestedByDecoderDict;
    std::shared_ptr<OBDDataDecoder> mOBDDataDecoder;
    // Mutex to ensure atomic decoder dictionary shared pointer content assignment
    std::mutex mDecoderDictMutex;
    // shared pointer to decoder dictionary
    std::shared_ptr<OBDDecoderDictionary> mDecoderDictionaryPtr;

    // Sleep backoff time in seconds for retrial in case an error is encountered during ECU auto-detection.
    static constexpr int SLEEP_TIME_SECS = 1;
    static constexpr uint32_t MASKING_GET_BYTE = 0xFF;             // Get last byte
    static constexpr uint32_t MASKING_SHIFT_BITS = 8;              // Shift 8 bits
    static constexpr uint32_t MASKING_TEMPLATE_TX_ID = 0x18DA00F1; // All 29-bit tx id has the same bytes
    static constexpr uint32_t MASKING_REMOVE_BYTE = 0x8;
    static constexpr uint32_t P2_TIMEOUT_DEFAULT_MS = 1000; // Set 1000 milliseconds timeout for ECU auto detection
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
