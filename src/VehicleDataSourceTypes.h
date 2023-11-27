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
    RAW_SOCKET,
    COMPLEX_DATA
    // Add any new protocols to the list of supported protocols below
};

constexpr std::array<VehicleDataSourceProtocol, 3> SUPPORTED_NETWORK_PROTOCOL = {
    { VehicleDataSourceProtocol::RAW_SOCKET,
      VehicleDataSourceProtocol::OBD,
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
      VehicleDataSourceProtocol::COMPLEX_DATA
#endif
    } };

} // namespace IoTFleetWise
} // namespace Aws
