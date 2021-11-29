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
#include "datatypes/CANRawMessage.h"
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
bool
CANRawMessage::setup( const std::uint32_t &id,
                      const std::uint8_t &dlc,
                      const std::uint8_t frameData[],
                      const timestampT &timestamp )
{
    bool result = false;

    mTimestamp = timestamp;
    mID = id;
    mDLC = dlc;
    if ( isValid() )
    {
        mFrameData.reserve( dlc );
        for ( size_t i = 0; i < dlc; ++i )
        {
            mFrameData.emplace_back( frameData[i] );
        }
        result = true;
    }

    return result;
}

CANRawMessage::CANRawMessage()
{
}

CANRawMessage::~CANRawMessage()
{
}

std::string
CANRawMessage::getStringRep()
{
    std::string frameString = std::to_string( mID ) + "#";

    for ( size_t i = 0; i < mDLC; ++i )
    {
        frameString += std::to_string( mFrameData[i] );
    }
    frameString += "#" + std::to_string( mTimestamp );
    return frameString;
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
