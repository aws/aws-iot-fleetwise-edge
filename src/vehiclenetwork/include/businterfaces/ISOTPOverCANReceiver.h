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

#pragma once

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "ClockHandler.h"
#include "LoggingModule.h"
#include "Timer.h"
#include "datatypes/ISOTPOverCANOptions.h"
#include <iostream>
#include <vector>

using namespace Aws::IoTFleetWise::Platform;

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
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
    /**
     * @brief Default Constructor/Destructor.
     */
    ISOTPOverCANReceiver();
    virtual ~ISOTPOverCANReceiver();

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

private:
    ISOTPOverCANReceiverOptions mReceiverOptions;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{};
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX