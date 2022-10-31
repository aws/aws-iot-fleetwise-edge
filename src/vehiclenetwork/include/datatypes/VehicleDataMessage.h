// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "TimeTypes.h"

#include <boost/any.hpp>
#include <cstdint>
#include <string>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

using namespace Aws::IoTFleetWise::Platform::Linux;

/**
 * @brief Generic Structure of a Vehicle Data Message.
 * Every Message has two sub structures i.e. Raw and Synthetic.
 */
class VehicleDataMessage
{
public:
    /**
     * @brief Constructor/destructor
     */
    inline VehicleDataMessage() = default;

    inline ~VehicleDataMessage() = default;

    /**
     * @brief Unique identifier of the Vehicle Data Message.
     * @return ID
     */
    inline std::uint64_t
    getMessageID() const
    {
        return mID;
    }

    /**
     * @brief Raw representation of the Message
     * @return Byte array of the raw data
     */
    // TODO: This API implies a copy of the data from the Network.
    // In certain Protocol stacks, the raw data can be a shared object.
    // Instead of Copying, we can use the shared object and work on it instead.
    // This interface is also not optimized for Standard CAN frames, as the size does not
    // exceed 8 Bytes. Using a Vector introduces an overhead of 8 extra bytes for the vector handle.
    inline const std::vector<std::uint8_t> &
    getRawData()
    {
        return mRawData;
    }

    /**
     * @brief Synthetic representation of the Message
     * @return Array of Any type
     */
    inline const std::vector<boost::any> &
    getSyntheticData()
    {
        return mSyntheticData;
    }

    /**
     * @brief Timepoint when the Message was acquired from the vehicle
     * @return Timestamp
     */
    inline const Timestamp &
    getReceptionTimestamp() const
    {
        return mTimestamp;
    }

    /**
     * @brief Checks if the Message is valid or not.
     * A message is valid if it have either a synthetic or a raw representation
     * @return True if Valid, False if not.
     */
    inline bool
    isValid() const
    {
        return !mSyntheticData.empty() || !mRawData.empty();
    }

    /**
     * @brief Routine to setup a Vehicle Data Message.
     */
    inline void
    setup( const std::uint32_t &id,
           const std::vector<std::uint8_t> &rawData,
           const std::vector<boost::any> &syntheticData,
           const Timestamp &timestamp )
    {
        mTimestamp = timestamp;
        mID = id;
        mRawData = rawData;
        mSyntheticData = syntheticData;
    }

private:
    std::uint64_t mID{};
    std::vector<std::uint8_t> mRawData;
    std::vector<boost::any> mSyntheticData;
    Timestamp mTimestamp{};
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
