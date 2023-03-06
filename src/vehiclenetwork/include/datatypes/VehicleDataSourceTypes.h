// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
    INVALID_ECU_ID = 0,
    BROADCAST_ID = 0x7DF,
    BROADCAST_EXTENDED_ID = 0x18DB33F1,
    LOWEST_ECU_EXTENDED_RX_ID = 0x18DAF100,
    LOWEST_ECU_RX_ID = 0x7E8,
    HIGHEST_ECU_EXTENDED_RX_ID = 0x18DAF1FF,
    HIGHEST_ECU_RX_ID = 0x7EF,
};

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
