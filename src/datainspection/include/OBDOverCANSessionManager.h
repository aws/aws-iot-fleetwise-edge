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

// Includes
#include "ClockHandler.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "businterfaces/ISOTPOverCANSenderReceiver.h"
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform;
using namespace Aws::IoTFleetWise::VehicleNetwork;
/**
 * @brief This module establish a OBD Diagnostics session between FWE and the ECU network.
 * This module initializes the OBD Session via a ISO-TP broadcast
 * message.
 * Only 1 CAN IF is supported, as the OBD stack sits on the Gateway bus.
 */
class OBDOverCANSessionManager
{
public:
    OBDOverCANSessionManager();
    virtual ~OBDOverCANSessionManager();
    /**
     * @brief Initializes the OBD Session.
     * @param gatewayCanInterfaceName CAN IF Name where the OBD stack on the ECU
     * is running. Typically on the Gateway ECU.
     * @return True if successful. False otherwise.
     */
    bool init( const std::string &gatewayCanInterfaceName );

    /**
     * @brief Creates an ISO-TP connection to the Engine ECU. Starts the
     * Keep Alive cyclic tread.
     * @return True if successful. False otherwise.
     */
    bool connect();

    /**
     * @brief Closes the ISO-TP connection to the Engine ECU. Stops the
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

    /**
     * @brief Issue an OBD heartbeat CAN Frame.
     * @return True if successful. False otherwise.
     */
    bool sendHeartBeat();

private:
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    ISOTPOverCANSenderReceiver mSenderReceiver;
    std::vector<uint8_t> mTxPDU;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws