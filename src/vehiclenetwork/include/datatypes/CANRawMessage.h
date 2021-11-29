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
#include <cstdint>
#include <string>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
// Timestamp unit in milliseconds
using timestampT = std::uint64_t;
/**
 * @brief Structure of a CAN Messages.
 */

class CANRawMessage
{
public:
    CANRawMessage();
    ~CANRawMessage();
    /**
     * @brief Setup a CAN Frame using the ID, DLC and the actual Frame data.
     * @param id CAN ID.
     * @param dlc Frame data length in bytes
     * @param frameData Array of Frame data bytes.
     * @param timestamp time of reception
     * @return returns false if the message is not valid (i.e. dlc is zero)
     */
    bool setup( const std::uint32_t &id,
                const std::uint8_t &dlc,
                const std::uint8_t frameData[],
                const timestampT &timestamp );

    /**
     * @brief returns the CAN message as a String. ( ID#FRAMEBYTES ). Not meant for
     * production usage.
     * @return string rep of the CAN Message
     */
    std::string getStringRep();

    inline const std::uint32_t &
    getMessageID()
    {
        return mID;
    }

    inline const std::uint8_t &
    getMessageDLC()
    {
        return mDLC;
    }

    inline const std::vector<std::uint8_t> &
    getMessageFrame()
    {
        return mFrameData;
    }

    inline const timestampT &
    getReceptionTimestamp()
    {
        return mTimestamp;
    }

    inline bool
    isValid() const
    {
        return mDLC > 0U && mDLC <= 8U;
    }

private:
    std::uint32_t mID{};
    std::uint8_t mDLC{};
    std::vector<std::uint8_t> mFrameData;
    timestampT mTimestamp{};
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
