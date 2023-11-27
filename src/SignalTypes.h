// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/** @brief Number of bits in a byte */
static constexpr uint8_t BYTE_SIZE = 8;

/**
 * @brief CAN Raw Frame ID is the arbitration ID of a CAN frame found on a bus. Paired with a NodeID its unique.
 */
using CANRawFrameID = uint32_t;

/**
 * @brief CAN Channel Numeric ID specifies which physical CAN channel a signal is found on. Its is only used internally
 * and not by any input or output artifact. Every vehicle has an array of available CAN channels, and the
 * CANChannelNumericID is the index to that array. CANChannelNumericID has a 1:1 mapping with CANInterfaceID. The array
 * of available channels is constructed during the FWE Binary launch by a config file passed to the FWE Binary.
 */
using CANChannelNumericID = uint32_t;
static constexpr CANChannelNumericID INVALID_CAN_SOURCE_NUMERIC_ID = 0xFFFFFFFF;

using CANInterfaceID = std::string;
static const CANInterfaceID INVALID_CAN_INTERFACE_ID{};

/**
 * @brief Signal ID is either an ID provided by Cloud that is unique across all signals found in the vehicle regardless
 * of network bus or an internal ID see INTERNAL_SIGNAL_ID_BITMASK
 */
using SignalID = uint32_t;
static constexpr SignalID INVALID_SIGNAL_ID = 0xFFFFFFFF;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
/**
 * If this MSB is set the SignalID is an internal ID that is only valid while the process is running.
 * An internal ID can not be used to store data to disk that should be read after restarting the process
 * or to send the data to the cloud.
 * If this MSB is not set than its an ID provided by cloud that together with the decoder manifest ARN is unique
 * and always valid.
 */
static constexpr SignalID INTERNAL_SIGNAL_ID_BITMASK = 0x80000000;

/**
 * @brief Internally generated Id used for example when partial signal paths are used. This makes it easier
 * to pass the value instead of passing the full signal path or a shared pointer.
 * Always the INTERNAL_SIGNAL_ID_BITMASK has to be set.
 */
using PartialSignalID = SignalID;
#endif

/**
 * @brief VSS supported datatypes
 * https://covesa.github.io/vehicle_signal_specification/rule_set/data_entry/data_types/
 * We currently supports 11 datatypes
 * We don't support string yet until cloud can support it
 */
enum struct SignalType
{
    UINT8 = 0,
    INT8 = 1,
    UINT16 = 2,
    INT16 = 3,
    UINT32 = 4,
    INT32 = 5,
    UINT64 = 6,
    INT64 = 7,
    FLOAT = 8,
    DOUBLE = 9,
    BOOLEAN = 10,
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    RAW_DATA_BUFFER_HANDLE = 11, // internal type RawData::BufferHandle is defined as uint32
#endif
};

/**
 * @brief Format that defines a CAN Signal Format
 */
struct CANSignalFormat
{
    /**
     * @brief Unique Signal ID provided by Cloud
     */
    uint32_t mSignalID{ 0x0 };

    /**
     * @brief Bool specifying endianness of data
     */
    bool mIsBigEndian{ false };

    /**
     * @brief Bool specifying whether signal is signed
     */
    bool mIsSigned{ false };

    /**
     * @brief The first bit position in bits
     */
    uint16_t mFirstBitPosition{ 0 };

    /**
     * @brief The size in bits of the signal.
     */
    uint16_t mSizeInBits{ 0 };

    /**
     * @brief The offset in the signal calculation (raw * mFactor) + mOffset
     */
    double mOffset{ 0 };

    /**
     * @brief The factor in the signal calculation (raw * mFactor) + mOffset
     */
    double mFactor{ 0 };

    /**
     * @brief Indicates whether the signal is the actual mux signal in the frame.
     */
    bool mIsMultiplexorSignal{ false };

    /**
     * @brief The datatype of the signal. The default is double for backward compatibility
     */
    SignalType mSignalType{ SignalType::DOUBLE };

    /**
     * @brief If mIsMultiplexorSignal is true, this value will be the value e.g. m0. If false, the value will be maxbit8
     */
    uint8_t mMultiplexorValue{ UINT8_MAX };

    /**
     * @brief Check if Signal is a multiplexer signal.
     * @return True if multiplier signal, false otherwise.
     */
    inline bool
    isMultiplexor() const
    {
        return mIsMultiplexorSignal;
    }

    /**
     * @brief Overloaded == operator for CANSignalFormat.
     * @param other Other CANSignalFormat to compare to.
     * @return True if ==, false otherwise.
     */
    bool
    operator==( const CANSignalFormat &other ) const
    {
        return ( mSignalID == other.mSignalID ) && ( mIsBigEndian == other.mIsBigEndian ) &&
               ( mIsSigned == other.mIsSigned ) && ( mFirstBitPosition == other.mFirstBitPosition ) &&
               ( mSizeInBits == other.mSizeInBits ) && ( mOffset == other.mOffset ) && ( mFactor == other.mFactor ) &&
               ( mIsMultiplexorSignal == other.mIsMultiplexorSignal ) &&
               ( mMultiplexorValue == other.mMultiplexorValue );
    }

    /**
     * @brief Overloaded != operator for CANSignalFormat.
     * @param other Other CANSignalFormat to compare to.
     * @return True if !=, false otherwise.
     */
    bool
    operator!=( const CANSignalFormat &other ) const
    {
        return !( *this == other );
    }
};

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
namespace RawData
{

using BufferHandle = uint32_t;
static constexpr BufferHandle INVALID_BUFFER_HANDLE = 0;

} // namespace RawData
#endif

} // namespace IoTFleetWise
} // namespace Aws
