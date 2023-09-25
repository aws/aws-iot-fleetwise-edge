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
 * This is the Sender API. API offers routines to send PDUs on the
 * CAN Bus in a point to point fashion. This API manages exactly
 * one Socket between the source and the destination.
 * This API is designed to be used from 1 single thread.
 * This API is mainly designed for testing purposes.
 */
class ISOTPOverCANSender
{
public:
    virtual ~ISOTPOverCANSender() = default;

    /**
     * @brief Initialize the Sender state.
     * @param senderOptions options to be used to create the P2P channel.
     * @return True if success, False otherwise
     */
    bool init( const ISOTPOverCANSenderOptions &senderOptions );

    /**
     * @brief Create and bind the Socket between the source and the destination
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
     * @brief Sends the PDU over the channel. This API blocks till
     *        all bytes in the PDU are transmitted.
     * @param pduData byte array
     * @return True if send, False otherwise
     */
    bool sendPDU( const std::vector<uint8_t> &pduData ) const;

private:
    ISOTPOverCANSenderOptions mSenderOptions;
    Timer mTimer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{};
};

} // namespace IoTFleetWise
} // namespace Aws
