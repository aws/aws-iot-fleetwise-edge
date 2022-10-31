// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
using namespace Aws::IoTFleetWise::Platform::Linux;
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
    OBDOverCANSessionManager() = default;
    ~OBDOverCANSessionManager() = default;

    OBDOverCANSessionManager( const OBDOverCANSessionManager & ) = delete;
    OBDOverCANSessionManager &operator=( const OBDOverCANSessionManager & ) = delete;
    OBDOverCANSessionManager( OBDOverCANSessionManager && ) = delete;
    OBDOverCANSessionManager &operator=( OBDOverCANSessionManager && ) = delete;

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