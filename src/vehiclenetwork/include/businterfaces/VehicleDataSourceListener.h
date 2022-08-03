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
#include "datatypes/VehicleDataSourceTypes.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

/**
 * @brief Vehicle Data Source Listener callback.
 *  These events are raised when the Connector got connected or
 *  disconnected from the Network Interface it listens to.
 */
struct VehicleDataSourceListener
{
    /**
     * @brief Default Destructor.
     */
    virtual ~VehicleDataSourceListener() = default;

    /**
     * @brief Callback raised if the Data Source is connected
     */
    virtual void onVehicleDataSourceConnected( const VehicleDataSourceID &vehicleDataSourceId ) = 0;

    /**
     * @brief Callback raised if the Data Source is disconnected
     */
    virtual void onVehicleDataSourceDisconnected( const VehicleDataSourceID &vehicleDataSourceId ) = 0;
};

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
