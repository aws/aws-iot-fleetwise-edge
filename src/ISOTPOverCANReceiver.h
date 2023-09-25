// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "ISOTPOverCANOptions.h"
#include "Timer.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief User Space API wrapping the ISO-TP Linux Kernel Module.
 * This is the Receiver API. API offers routines to receive PDUs on the
 * CAN Bus in a point to point fashion. This API manages exactly
 * one Socket between the source and the destination.
 * This API is designed to be used from 1 single thread.
 * This API is mainly designed for testing purposes.
 */
class ISOTPOverCANReceiver
{
public:
    virtual ~ISOTPOverCANReceiver() = default;

    /**
     * @brief Initialize the Receiver state.
     * @param receiverOptions options to be used to create the P2P channel.
     * @return True if success, False otherwise
     */
    bool init( const ISOTPOverCANReceiverOptions &receiverOptions );

    /**
     * @brief Create the Socket between the source and the destination
     * @return True if success, False otherwise
     */
    bool connect();

    /**
     * @brief Close the Socket between the source and the destination
     * @return True if success, False otherwise
     */
    bool disconnect() const;

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
     * @brief Returns the socket
     * @return Socket
     */
    int
    getSocket() const
    {
        return mSocket;
    }

private:
    ISOTPOverCANReceiverOptions mReceiverOptions;
    Timer mTimer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{};
};

} // namespace IoTFleetWise
} // namespace Aws
