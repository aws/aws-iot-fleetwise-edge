// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <cstdint>

namespace Aws
{
namespace IoTFleetWise
{

struct CANDecodedSignal
{

    CANDecodedSignal( uint32_t signalID, DecodedSignalValue physicalValue, SignalType signalTypeIn )
        : mSignalID( signalID )
        , mPhysicalValue( physicalValue )
        , mSignalType( signalTypeIn )
    {
    }

    uint32_t mSignalID;
    DecodedSignalValue mPhysicalValue;
    SignalType mSignalType{ SignalType::UNKNOWN };
};

/**
 * @brief Cloud does not send information about each CAN message, so we set every CAN message size to the maximum.
 */
static constexpr uint8_t MAX_CAN_FRAME_BYTE_SIZE = 64;

} // namespace IoTFleetWise
} // namespace Aws
