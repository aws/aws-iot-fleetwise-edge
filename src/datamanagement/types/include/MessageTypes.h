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

#include "SignalTypes.h"
#include <cstdint>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
/**
 * @brief Message ID is an ID provided by Cloud that is unique across all messages found in the vehicle regardless of
 * network bus. Note that this is not the ID of CAN message.
 */
using MessageID = uint64_t;

static constexpr MessageID MESSAGE_ID_NA = 0xFFFFFFFE;
static constexpr MessageID INVALID_MESSAGE_ID = 0xFFFFFFFF;
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
        return mMessageID == other.mMessageID && mSizeInBytes == other.mSizeInBytes && mSignals == other.mSignals &&
               mIsMultiplexed == other.mIsMultiplexed;
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

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws