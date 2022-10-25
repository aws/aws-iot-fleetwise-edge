// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "TimeTypes.h"
#include "datatypes/VehicleDataSourceTypes.h"
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::Platform::Linux;

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
    uint32_t mFrameID{ 0 };
    std::string mFrameRawData;
    std::vector<CANDecodedSignal> mSignals;
};
/**
 * @brief Cloud does not send information about each CAN message, so we set every CAN message size to the maximum.
 */
static constexpr uint8_t MAX_CAN_FRAME_BYTE_SIZE = 8;

struct CANDecodedMessage
{
    CANFrameInfo mFrameInfo;
    Timestamp mReceptionTime{ 0 };
    Timestamp mDecodingTime{ 0 };
    VehicleDataSourceIfName mChannelIfName;
    VehicleDataSourceType mChannelType;
    VehicleDataSourceProtocol mChannelProtocol;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
