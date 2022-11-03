// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "ClockHandler.h"
#include "LoggingModule.h"
#include "Timer.h"
#include "datatypes/ISOTPOverCANOptions.h"
#include <iostream>
#include <vector>

using namespace Aws::IoTFleetWise::Platform::Linux;

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
/**
 * @brief User Space API wrapping the ISO-TP Linux Kernel Module.
 * This is the Sender/Receiver API. API offers routines to send
 * and receive PDUs on the CAN Bus in a point to point fashion.
 * This API manages exactly one Socket between the source and the destination.
 * The send and receive APIs can be used from different thread.
 * Thread safety is guaranteed by at the Kernel level ( atomic operations ).
 */
class ISOTPOverCANSenderReceiver
{
public:
    virtual ~ISOTPOverCANSenderReceiver() = default;

    /**
     * @brief Initialize the Sender/Receiver state.
     * @param senderReceiverOptions options to be used to create the P2P channel.
     * @return True if success, False otherwise
     */
    bool init( const ISOTPOverCANSenderReceiverOptions &senderReceiverOptions );

    /**
     * @brief Create the Socket between the source and the destination
     * @return True if success, False otherwise
     */
    bool connect();

    /**
     * @brief Close the Socket between the source and the destination
     * @return True if success, False otherwise
     */
    bool disconnect();

    /**
     * @brief Checks the health state of the connection.
     * @return True if healthy, False otherwise
     */
    bool isAlive() const;

    /**
     * @brief Receives PDUs over the channel. This API blocks
     *        till all bytes in the PDU are consumed.
     *        There is no guarantee that all PDUs will be filled in the pduData.
     * @param pduData  byte array where the PDU data will be filled.
     *                 Callers can expect up to 4095 Bytes at most.
     * @return True if the PDU is received, False otherwise.
     */
    bool receivePDU( std::vector<uint8_t> &pduData );

    /**
     * @brief Sends the PDU over the channel. This API blocks till
     *        all bytes in the PDU are transmitted.
     * @param pduData byte array
     * @return True if send, False otherwise
     */
    bool sendPDU( const std::vector<uint8_t> &pduData );

private:
    ISOTPOverCANSenderReceiverOptions mSenderReceiverOptions;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{};
    std::string mStreamRxID;
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX