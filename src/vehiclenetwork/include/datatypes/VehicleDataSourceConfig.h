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
