// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

namespace Aws
{
namespace IoTFleetWise
{

// Transport Protocol used by the Vehicle Data Source
enum class VehicleDataSourceProtocol
{
    INVALID_PROTOCOL,
    OBD,
    RAW_SOCKET
    // Add any new protocols to the list of supported protocols below
};

constexpr std::array<VehicleDataSourceProtocol, 2> SUPPORTED_NETWORK_PROTOCOL = {
    { VehicleDataSourceProtocol::RAW_SOCKET, VehicleDataSourceProtocol::OBD } };

} // namespace IoTFleetWise
} // namespace Aws
