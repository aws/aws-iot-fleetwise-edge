// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "VehicleDataSourceTypes.h"
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

/**
 * @brief A structure representing the configuration of a Vehicle Data Source.
 * @param maxNumberOfVehicleDataMessages The Maximum number of vehicle data messages that this
 * source can make available at a given point in time.
 * @param  vehicleDataMessageFilter  Regular expression representing a filter for the source to
 * discard messages by e.g. message ID, topic.
 * @param transportProperties Container for any other Transport properties eg. CAN Bus interface
 * Topic Name.
 */
struct VehicleDataSourceConfig
{
    uint32_t maxNumberOfVehicleDataMessages;
    std::regex vehicleDataMessageFilter;
    std::map<std::string, std::string> transportProperties;
};

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
