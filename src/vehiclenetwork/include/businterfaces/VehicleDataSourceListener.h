// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
