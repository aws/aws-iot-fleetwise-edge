// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SignalTypes.h"
#include <cstdint>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

static constexpr SignalID INVALID_CAN_FRAME_ID = 0xFFFFFFFF;

/**
 * @brief Contains the decoding rules to decode all signals in a CAN frame.
 */
struct CANMessageFormat
{
    uint32_t mMessageID{ 0x0 };
    uint8_t mSizeInBytes{ 0 };
    bool mIsMultiplexed{ false };
    std::vector<CANSignalFormat> mSignals;

public:
    /**
     * @brief Overload of the == operator
     * @param other Other CANMessageFormat object to compare
     * @return true if ==, false otherwise
     */
    bool
    operator==( const CANMessageFormat &other ) const
    {
        return ( mMessageID == other.mMessageID ) && ( mSizeInBytes == other.mSizeInBytes ) &&
               ( mSignals == other.mSignals ) && ( mIsMultiplexed == other.mIsMultiplexed );
    }

    /**
     * @brief Overload of the != operator
     * @param other Other CANMessageFormat object to compare
     * @return true if !=, false otherwise
     */
    bool
    operator!=( const CANMessageFormat &other ) const
    {
        return !( *this == other );
    }

    /**
     * @brief Check if a CAN Message Format is value by making sure it contains at least one signal
     * @return True if valid, false otherwise.
     */
    inline bool
    isValid() const
    {
        return !mSignals.empty();
    }

    /**
     * @brief Check if a CAN Message Decoding rule is multiplexed
     * @return True if multiplexed, false otherwise.
     */
    inline bool
    isMultiplexed() const
    {
        return mIsMultiplexed;
    }
};

} // namespace IoTFleetWise
} // namespace Aws
