// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SignalTypes.h"
#include "TimeTypes.h"
#include <cstdint>

namespace Aws
{
namespace IoTFleetWise
{

union CANPhysicalValue {
    double doubleVal;
    uint64_t uint64Val;
    int64_t int64Val;
};

struct CANPhysicalValueType
{
    CANPhysicalValue signalValue;
    SignalType signalType;

    template <typename T>
    CANPhysicalValueType( T val, SignalType type )
        : signalType( type )
    {
        switch ( signalType )
        {
        case SignalType::UINT64:
            signalValue.uint64Val = static_cast<uint64_t>( val );
            break;
        case SignalType::INT64:
            signalValue.int64Val = static_cast<int64_t>( val );
            break;
        default:
            signalValue.doubleVal = static_cast<double>( val );
        }
    }

    SignalType
    getType() const
    {
        return signalType;
    }
};

struct CANDecodedSignal
{

    CANDecodedSignal( uint32_t signalID, int64_t rawValue, CANPhysicalValueType physicalValue, SignalType signalTypeIn )
        : mSignalID( signalID )
        , mRawValue( rawValue )
        , mPhysicalValue( physicalValue )
        , mSignalType( signalTypeIn )
    {
    }

    uint32_t mSignalID;
    int64_t mRawValue;
    CANPhysicalValueType mPhysicalValue;
    SignalType mSignalType{ SignalType::DOUBLE };
};

/**
 * @brief Cloud does not send information about each CAN message, so we set every CAN message size to the maximum.
 */
static constexpr uint8_t MAX_CAN_FRAME_BYTE_SIZE = 64;

} // namespace IoTFleetWise
} // namespace Aws
