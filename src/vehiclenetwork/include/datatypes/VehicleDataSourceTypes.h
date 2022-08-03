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
#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
// Identifier of the Vehicle Data Source
using VehicleDataSourceID = uint32_t;
const VehicleDataSourceID INVALID_DATA_SOURCE_ID = 0;

using VehicleDataSourceIfName = std::string;
// Types of the the Vehicle Data Source
enum class VehicleDataSourceType
{
    INVALID_SOURCE_TYPE,
    CAN_SOURCE,
    CAN_FD_SOURCE,
    FLEXRAY_SOURCE,
    LIN_SOURCE,
    ETHERNET_SOURCE,
    IPC_SOURCE
};

// Transport Protocol used by the Vehicle Data Source
enum class VehicleDataSourceProtocol
{
    INVALID_PROTOCOL,
    CAN_TP,
    SOMEIP,
    OBD,
    RAW_SOCKET,
    DOIP,
    AVB,
    DDS
};

// Vehicle Data Source States
enum class VehicleDataSourceState
{
    INVALID_STATE,
    CONNECTED,
    DISCONNECTED
};

// ECU IDs
enum class ECUID
{
    INVALID_ECU_ID,
    ENGINE_ECU_TX = 0x7E0,
    ENGINE_ECU_RX = 0x7E8,
    ENGINE_ECU_TX_EXTENDED = 0x18DB33F1,
    ENGINE_ECU_RX_EXTENDED = 0x18DAF158,
    TRANSMISSION_ECU_TX = 0x7E1,
    TRANSMISSION_ECU_TX_EXTENDED = 0x18DA59F1,
    TRANSMISSION_ECU_RX = 0x7E9,
    TRANSMISSION_ECU_RX_EXTENDED = 0x18DAF159,
    ECU3_TX = 0x7E2,
    ECU3_RX = 0x7EA,
    ECU4_TX = 0x7E3,
    ECU4_RX = 0x7EB,
    ECU5_TX = 0x7E4,
    ECU5_RX = 0x7EC,
    ECU6_TX = 0x7E5,
    ECU6_RX = 0x7ED,
    ECU7_TX = 0x7E6,
    ECU7_RX = 0x7EE,
    ECU8_TX = 0x7E7,
    ECU8_RX = 0x7EF,
    BROADCAST_ID = 0x7DF,
    BROADCAST_EXTENDED_ID = 0x18DB33F1
};
// ECU Type
enum class ECUType
{
    ENGINE,
    TRANSMISSION
};

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws