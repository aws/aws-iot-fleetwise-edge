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
 * CANChannelNumericID is the index to that array. CANChannelNumericID has a 1:1 mapping with InterfaceID. The array
 * of available channels is constructed during the FWE Binary launch by a config file passed to the FWE Binary.
 */
using CANChannelNumericID = uint32_t;
static constexpr CANChannelNumericID INVALID_CAN_SOURCE_NUMERIC_ID = 0xFFFFFFFF;

/**
 * @brief Interface ID is a string identifier for a logical network interface that is defined in the static
 * configuration file, and must match the value sent by cloud in the decoder manifest.
 */
using InterfaceID = std::string;
static const InterfaceID INVALID_INTERFACE_ID{};

/**
 * @brief The sync ID is a concatenation of the resource ARN, a slash '/', and a timestamp.
 * This ID is used to uniquely identify and synchronize documents between the cloud and edge.
 */
using SyncID = std::string;

/**
 * @brief Signal ID is either an ID provided by Cloud that is unique across all signals found in the vehicle regardless
 * of network bus or an internal ID see INTERNAL_SIGNAL_ID_BITMASK. Cloud starts allocating signal IDs starting at 1.
 */
using SignalID = uint32_t;
static constexpr SignalID INVALID_SIGNAL_ID = 0;

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
    UNKNOWN = 11,
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    COMPLEX_SIGNAL = 12, // internal type RawData::BufferHandle is defined as uint32
#endif
};

/**
 * @brief Converts SignalType to a string to be used in logs
 *
 * @param signalType The signal type to be converted.
 * @return A string describing the signal type
 */
inline std::string
signalTypeToString( SignalType signalType )
{
    // coverity[autosar_cpp14_m6_4_6_violation]
    // coverity[misra_cpp_2008_rule_6_4_6_violation] compiler warning is preferred over a default-clause
    switch ( signalType )
    {
    case SignalType::UINT8:
        return "UINT8";
    case SignalType::INT8:
        return "INT8";
    case SignalType::UINT16:
        return "UINT16";
    case SignalType::INT16:
        return "INT16";
    case SignalType::UINT32:
        return "UINT32";
    case SignalType::INT32:
        return "INT32";
    case SignalType::UINT64:
        return "UINT64";
    case SignalType::INT64:
        return "INT64";
    case SignalType::FLOAT:
        return "FLOAT";
    case SignalType::DOUBLE:
        return "DOUBLE";
    case SignalType::BOOLEAN:
        return "BOOLEAN";
    case SignalType::UNKNOWN:
        return "UNKNOWN";
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    case SignalType::COMPLEX_SIGNAL:
        return "COMPLEX_SIGNAL";
#endif
    }
    return "UNREACHABLE";
}

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
     * @brief The datatype of the signal.
     */
    SignalType mSignalType{ SignalType::UNKNOWN };

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

union DecodedSignalValueType {
    double doubleVal;
    uint64_t uint64Val;
    int64_t int64Val;
};

/**
 * @brief A signal value that was decoded by a data source
 *
 * The actual value can be of different types, but we don't consider all possible supported types
 * here. To simplify the decoding only the largest types are used to avoid losing precision.
 * Since this value is not intended to be stored for a long time, there is not much impact e.g. using
 * a double for a uint8_t.
 */
struct DecodedSignalValue
{
    DecodedSignalValueType signalValue;
    SignalType signalType;

    template <typename T>
    DecodedSignalValue( T val, SignalType type )
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
};

namespace RawData
{

using BufferHandle = uint32_t;
static constexpr BufferHandle INVALID_BUFFER_HANDLE = 0;

} // namespace RawData

} // namespace IoTFleetWise
} // namespace Aws
