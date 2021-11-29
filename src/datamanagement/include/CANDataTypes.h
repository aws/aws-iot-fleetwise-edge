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
#include "datatypes/NetworkChannelDataTypes.h"
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::VehicleNetwork;

// Timestamp unit in milliseconds
using timestampT = std::uint64_t;

struct CANDecodedSignal
{
    CANDecodedSignal( uint32_t signalID, int64_t rawValue, double physicalValue )
        : mSignalID( signalID )
        , mRawValue( rawValue )
        , mPhysicalValue( physicalValue )
    {
    }

    uint32_t mSignalID;
    int64_t mRawValue;
    double mPhysicalValue;
};

struct CANFrameInfo
{
    uint32_t mFrameID;
    std::string mFrameRawData;
    std::vector<CANDecodedSignal> mSignals;
};

struct CANDecodedMessage
{
    CANFrameInfo mFrameInfo;
    timestampT mReceptionTime;
    timestampT mDecodingTime;
    NetworkChannelIfName mChannelIfName;
    NetworkChannelType mChannelType;
    NetworkChannelProtocol mChannelProtocol;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
