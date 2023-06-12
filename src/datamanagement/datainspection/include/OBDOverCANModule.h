// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "IActiveConditionProcessor.h"
#include "IActiveDecoderDictionaryListener.h"
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

// ECU IDs
enum class ECUID
{
    INVALID_ECU_ID = 0,
    BROADCAST_ID = 0x7DF,
    BROADCAST_EXTENDED_ID = 0x18DB33F1,
    LOWEST_ECU_EXTENDED_RX_ID = 0x18DAF100,
    LOWEST_ECU_RX_ID = 0x7E8,
    HIGHEST_ECU_EXTENDED_RX_ID = 0x18DAF1FF,
    HIGHEST_ECU_RX_ID = 0x7EF,
};

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
     * @param broadcastRequests Broadcast requests to all ECUs - required by some vehicles
     * @return True if successful. False if both pidRequestIntervalSeconds
     * and dtcRequestIntervalSeconds are zero i.e. no collection
     */
    bool init( SignalBufferPtr signalBufferPtr,
               ActiveDTCBufferPtr activeDTCBufferPtr,
               const std::string &gatewayCanInterfaceName,
               uint32_t pidRequestIntervalSeconds,
               uint32_t dtcRequestIntervalSeconds,
               bool broadcastRequests );
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

    // From IActiveDecoderDictionaryListener
    // We need this to know whether PIDs should be requested or not
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

    // From IActiveConditionProcessor
    // We need this to know whether DTCs should be requested or not
    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix ) override;

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

    /**
     * @brief Gets a list of PIDs to request externally
     * @return List of PIDs
     */
    std::vector<PID> getExternalPIDsToRequest();

    /**
     * @brief Sets the response for the given PID
     * @param pid The PID
     * @param response The response
     */
    void setExternalPIDResponse( PID pid, std::vector<uint8_t> response );

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
    static constexpr uint32_t getTxIDByRxID( bool isExtendedID, uint32_t rxId );

    /**
     * @brief Initialize ECUs by detected CAN IDs
     * @param isExtendedID When true, 29-bit messages were detected, otherwise 11-bit
     * @param canIDResponses Detected CAN ID via broadcast request
     * @param broadcastSocket Broadcast socket for sending request. Can be -1 to send using physical socket.
     * @return True if all OBDOverCANECU modules are initialized successfully for ECUs
     */
    bool initECUs( bool isExtendedID, std::vector<uint32_t> &canIDResponses, int broadcastSocket );

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
    void assignPIDsToECUs();
    /**
     * @brief Open an ISO-TP broadcast socket to send requests for all ECUs
     * @param isExtendedID Whether the broadcast ID is in extended format (29-bit) or standard format (11 bit)
     * @return Broadcast socket, or -1 on error
     */
    int openISOTPBroadcastSocket( bool isExtendedID );
    /**
     * @brief Flush ecus sockets to ignore received data from broadcast request. If mBroadcastRequests is false,
     *        no flushing will be performed.
     * @param count The number of responses to be flushed
     * @param exceptECU Flush all ECUs except this one
     */
    void flush( size_t count, std::shared_ptr<OBDOverCANECU> &exceptECU );

    static void calcSleepTime( uint32_t requestIntervalSeconds, const Timer &timer, int64_t &outputSleepTime );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mDecoderManifestAvailable{ false };
    std::atomic<bool> mShouldRequestDTCs{ false };
    std::vector<std::shared_ptr<OBDOverCANECU>> mECUs;
    mutable std::mutex mThreadMutex;
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
    bool mBroadcastRequests{ false };
    int mBroadcastSocket{ -1 };
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
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
