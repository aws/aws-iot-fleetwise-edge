// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "IRemoteDiagnostics.h"
#include "ISOTPOverCANSenderReceiver.h"
#include "Signal.h"
#include "Thread.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

struct EcuConfig
{
    std::string ecuName;
    std::string canBus;
    uint32_t physicalRequestID{ 0 };
    uint32_t physicalResponseID{ 0 };
    uint32_t functionalAddress{ 0 };
    int32_t targetAddress{ 0 };
};

struct EcuConnectionInfo
{
    EcuConfig communicationParams;
    ISOTPOverCANSenderReceiver isotpSenderReceiver;
    std::vector<uint8_t> data;
};

struct UdsDtcRequest
{
    int32_t targetAddress{ 0 };
    std::vector<uint8_t> sendPDU;
    std::string token;
    UDSResponseCallback callback;
};

struct UdsDtcResponse
{
    UDSResponseCallback callback;
    DTCResponse response;
};

class ExampleUDSInterface : public IRemoteDiagnostics
{
public:
    ExampleUDSInterface() = default;
    ~ExampleUDSInterface() override;
    ExampleUDSInterface( const ExampleUDSInterface & ) = delete;
    ExampleUDSInterface &operator=( const ExampleUDSInterface & ) = delete;
    ExampleUDSInterface( ExampleUDSInterface && ) = delete;
    ExampleUDSInterface &operator=( ExampleUDSInterface && ) = delete;

    // Start the  thread
    bool start();
    // Stop the  thread
    bool stop();
    bool init( const std::vector<EcuConfig> &ecuConfigs );

    /**
     * @brief This function queues a new UDS DTC request for processing.
     *
     * @param request A const reference to a UdsDtcRequest object containing the details of the DTC request.
     */
    void addUdsDtcRequest( const UdsDtcRequest &request );

    void readDTCInfo( int32_t targetAddress,
                      UDSSubFunction subfn,
                      UDSStatusMask mask,
                      UDSResponseCallback callback,
                      const std::string &token ) override;
    void readDTCInfoByDTCAndRecordNumber( int32_t targetAddress,
                                          UDSSubFunction subfn,
                                          uint32_t dtc,
                                          uint8_t recordNumber,
                                          UDSResponseCallback callback,
                                          const std::string &token ) override;

private:
    // Intercepts stop signals.
    bool shouldStop() const;
    static void doWork( void *data );
    /**
     * @brief Returns the health state of the cyclic thread
     * @return True if successful. False otherwise.
     */
    bool isAlive();

    /**
     * @brief Executes a UDS (Unified Diagnostic Services) request.
     *
     * This function sends a UDS request PDU (Protocol Data Unit) to a specified target address
     * and processes the response.
     *
     * @param sendPDU A reference to a vector of uint8_t containing the request PDU to be sent.
     * @param targetAddress An integer specifying the target address for the request.
     * @param response A reference to an DTCResponse object to store the response data.
     * @return int Returns a status code indicating the result of the request execution.
     */
    bool executeRequest( std::vector<uint8_t> &sendPDU, int32_t targetAddress, DTCResponse &response );

    /**
     * @brief Finds a ECU configuration for a given target address.
     *
     * @param target An integer representing the target ECU identifier.
     * @param out A reference to an EcuConfig object where the found configuration will be stored.
     * @return bool Returns true if the target address is found, false otherwise.
     */
    bool findTargetAddress( int target, EcuConfig &out );

    // Stop signal
    Signal mWait;
    Thread mThread;
    mutable std::mutex mThreadMutex;
    mutable std::mutex mQueryMutex;
    std::atomic<bool> mShouldStop{ false };

    std::queue<UdsDtcRequest> mDtcRequestQueue;
    std::queue<UdsDtcResponse> mDtcResponseQueue;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    std::vector<EcuConfig> mEcuConfig;
};

} // namespace IoTFleetWise
} // namespace Aws
