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

// Includes
#include "businterfaces/NetworkChannelBridgeListener.h"
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
NetworkChannelBridgeListener::~NetworkChannelBridgeListener()
{
}
void
NetworkChannelBridgeListener::onNetworkChannelConnected( const NetworkChannelID &networkChannelId )
{
    (void)networkChannelId; // unused variable
}

void
NetworkChannelBridgeListener::onNetworkChannelDisconnected( const NetworkChannelID &networkChannelId )
{
    (void)networkChannelId; // unused variable
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
