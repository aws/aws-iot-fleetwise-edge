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

#include "OBDDataTypes.h"
#include <cstdint>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
/**
 * @brief number of bits in one byte
 */
constexpr uint8_t BYTE_SIZE = 8;

/**
 * @brief Cloud does not send information about each CAN message, so we set every CAN message size to the maximum.
 */
constexpr uint8_t MAX_CAN_FRAME_BYTE_SIZE = 8;

/**
 * @brief Signal ID is an ID provided by Cloud that is unique across all signals found in the vehicle regardless of
 * network bus.
 */
using SignalID = uint32_t;
const SignalID INVALID_SIGNAL_ID = 0xFFFFFFFF;

/**
 * @brief Identifier of an Image Sensor device.
 */
using ImageDeviceID = uint32_t;

/**
 * @brief Identifier of Event triggered by a collectionScheme.
 */
using EventID = uint32_t;

/**
 * @brief CAN Channel Numeric ID specifies which physical CAN channel a signal is found on. Its is only used internally
 * and not by any input or output artifact. Every vehicle has an array of available CAN channels, and the
 * CANChannelNumericID is the index to that array. CANChannelNumericID has a 1:1 mapping with CANInterfaceID. The array
 * of available channels is constructed during the FWE Binary launch by a config file passed to the FWE Binary.
 */
using CANChannelNumericID = uint16_t;
const CANChannelNumericID INVALID_CAN_CHANNEL_NUMERIC_ID = 0xFFFF;

using CANInterfaceID = std::string;
const CANInterfaceID INVALID_CAN_INTERFACE_ID{};

/**
 * @brief CAN Raw Frame ID is the arbitration ID of a CAN frame found on a bus. Paired with a NodeID its unique.
 */
using CANRawFrameID = uint32_t; //
const SignalID INVALID_CAN_FRAME_ID = 0xFFFFFFFF;

/**
 * @brief Format that defines a CAN Signal Format
 */
struct CANSignalFormat
{
    CANSignalFormat()
        : mSignalID( 0x0 )
        , mIsBigEndian( false )
        , mIsSigned( false )
        , mFirstBitPosition( 0 )
        , mSizeInBits( 0 )
        , mOffset( 0.0 )
        , mFactor( 0.0 )
        , mIsMultiplexorSignal( false )
        , mMultiplexorValue( UINT8_MAX )
    {
    }

    /**
     * @brief Unique Signal ID provided by Cloud
     */
    uint32_t mSignalID;

    /**
     * @brief Bool specifying endianness of data
     */
    bool mIsBigEndian;

    /**
     * @brief Bool specifying whether signal is signed
     */
    bool mIsSigned;

    /**
     * @brief The first bit position in bits
     */
    uint16_t mFirstBitPosition;

    /**
     * @brief The size in bits of the signal.
     */
    uint16_t mSizeInBits;

    /**
     * @brief The offset in the signal calculation (raw * mFactor) + mOffset
     */
    double mOffset;

    /**
     * @brief The factor in the signal calculation (raw * mFactor) + mOffset
     */
    double mFactor;

    /**
     * @brief Indicates whether the signal is the actual mux signal in the frame.
     */
    bool mIsMultiplexorSignal;

    /**
     * @brief If mIsMultiplexorSignal is true, this value will be the value e.g. m0. If false, the value will be maxbit8
     */
    uint8_t mMultiplexorValue;

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
        return mSignalID == other.mSignalID && mIsBigEndian == other.mIsBigEndian && mIsSigned == other.mIsSigned &&
               mFirstBitPosition == other.mFirstBitPosition && mSizeInBits == other.mSizeInBits &&
               mOffset == other.mOffset && mFactor == other.mFactor &&
               mIsMultiplexorSignal == other.mIsMultiplexorSignal && mMultiplexorValue == other.mMultiplexorValue;
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

/**
 * @brief Contains the decoding rules to decode all signals in a CAN frame.
 */
struct CANMessageFormat
{
    CANMessageFormat()
        : mMessageID( 0x0 )
        , mSizeInBytes( 0 )
        , mIsMultiplexed( false )
    {
    }

    uint32_t mMessageID;
    uint8_t mSizeInBytes;
    bool mIsMultiplexed;
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
        return mSignals.size() > 0;
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

/**
 * @brief An invalid CAN Message Format, set as a CANMessageFormat object initialized to all zeros
 */
const CANMessageFormat INVALID_CAN_MESSAGE_FORMAT = CANMessageFormat();

/**
 * @brief Contains the decoding rules to decode OBD-II PID Signals
 */
struct PIDSignalDecoderFormat
{
    /**
     * @brief default constructor with no input
     */
    PIDSignalDecoderFormat()
        : mPidResponseLength( 0 )
        , mServiceMode( INVALID_SERVICE_MODE )
        , mPID( 0 )
        , mScaling( 0 )
        , mOffset( 0 )
        , mStartByte( 0 )
        , mByteLength( 0 )
        , mBitRightShift( 0 )
        , mBitMaskLength( 0 ){};

    /**
     * @brief constructor with input to initialize all member variables
     */
    PIDSignalDecoderFormat( size_t pidResponseLength,
                            SID sid,
                            PID pid,
                            double scaling,
                            double offset,
                            size_t startByte,
                            size_t byteLength,
                            uint8_t bitRightShift,
                            uint8_t bitMaskLength )
        : mPidResponseLength( pidResponseLength )
        , mServiceMode( sid )
        , mPID( pid )
        , mScaling( scaling )
        , mOffset( offset )
        , mStartByte( startByte )
        , mByteLength( byteLength )
        , mBitRightShift( bitRightShift )
        , mBitMaskLength( bitMaskLength ){};

    /*
     * Length of the PID response. Note this is not the signal byte length as PID might contain
     * multiple signals
     */
    size_t mPidResponseLength;

    /*
     * OBDII-PID Service Mode for the signal in decimal
     */
    SID mServiceMode;

    /*
     * OBD request PID in decimal
     */
    PID mPID;

    /*
     * scaling to decode OBD from raw bytes to double value
     * e.g. A * 0.0125 - 40. scaling is 0.01
     */
    double mScaling;

    /*
     * offset to decode OBD from raw bytes to double value
     * e.g. A * 0.0125 - 40. offset is -40.0
     */
    double mOffset;

    /*
     * the start byte order (starting from 0th) for this signal in its PID query response
     * e.g. PID 0x14 contains two signals. SHRFT is the second byte. Its startByte is 1
     */
    size_t mStartByte;

    /*
     * number of bytes for this signal in its PID query response
     * e.g. PID 0x14 contains two signals. SHRFT is one byte. Its byteLength is 1
     */
    size_t mByteLength;

    /*
     * Right shift on bits to decode this signal from raw bytes. Note the bit manipulation is
     * only performed when byteLength is 1.
     * e.g. Boost Pressure B Control Status is bit 2, 3 on byte J. The right shift shall be 2
     *      For non-bitmask signals, the right shift shall always be 0
     */
    uint8_t mBitRightShift;

    /*
     * bit Mask Length to be applied to decode this signal from raw byte. Note the bit manipulation
     * is only performed when byteLength is 1.
     * e.g. Boost Pressure B Control Status is bit 2, 3 on byte J. The bit Mask Length would be 2
     *      For non-bitmask signals, the bit Mask Length shall always be 8.
     */
    uint8_t mBitMaskLength;

public:
    /**
     * @brief Overload of the == operator
     * @param other Other PIDSignalDecoderFormat object to compare
     * @return true if ==, false otherwise
     */
    bool
    operator==( const PIDSignalDecoderFormat &other ) const
    {
        return mPidResponseLength == other.mPidResponseLength && mServiceMode == other.mServiceMode &&
               mPID == other.mPID && mScaling == other.mScaling && mOffset == other.mOffset &&
               mStartByte == other.mStartByte && mByteLength == other.mByteLength &&
               mBitRightShift == other.mBitRightShift && mBitMaskLength == other.mBitMaskLength;
    }
};

/**
 * @brief Error Code for OBD-II PID Decoder Format Not Ready to read
 */
const PIDSignalDecoderFormat NOT_READY_PID_DECODER_FORMAT = PIDSignalDecoderFormat();

/**
 * @brief Error Code for OBD-II PID Decoder Format Not Found in decoder manifest
 */
const PIDSignalDecoderFormat NOT_FOUND_PID_DECODER_FORMAT = PIDSignalDecoderFormat();

/**
 * @brief IDecoderManifest is used to exchange DecoderManifest between components
 *
 * This is separated from ICollectionScheme to make it possible to also decode messages (for
 * example for debug purpose) that are currently not collected from any Collection Scheme
 *
 */
class IDecoderManifest
{
public:
    /**
     * @brief indicates if the decoder manifest is prepared to be used for example by calling getters
     *
     * @return true if ready and false if not ready then build function must be called first
     * */
    virtual bool isReady() const = 0;

    /**
     * @brief Build internal structures from raw input so lazy initialization is possible
     *
     * @return true if build succeeded false if the collectionScheme is corrupted and can not be used
     * */
    virtual bool build() = 0;

    /**
     * @brief Get the ID of the decoder manifest
     *
     * @return String ID of the decoder manifest. Empty string if error.
     */
    virtual std::string getID() const = 0;

    /**
     * @brief get CAN Message format to decode.
     * @param canID msg id seen in the frame on the bus
     * @param interfaceID the channel on which the frame was received
     * @return if can frame id can't be found a CANMessageFormat equal to INVALID_CAN_MESSAGE_FORMAT
     * is returned
     */
    virtual const CANMessageFormat &getCANMessageFormat( CANRawFrameID canID, CANInterfaceID interfaceID ) const = 0;

    /**
     * @brief get the can frame that contains the signal
     * @param signalID unique signal id
     *
     * @return if no can and can interface ids can be found invalid ids are returned
     */
    virtual std::pair<CANRawFrameID, CANInterfaceID> getCANFrameAndInterfaceID( SignalID signalID ) const = 0;

    /**
     * @brief Get the Network Channel Protocol for this Signal
     * @param signalID the unique signalID
     * @return invalid Protocol Type if signal is not found in decoder manifest
     */
    virtual NetworkChannelProtocol getNetworkProtocol( SignalID signalID ) const = 0;

    /**
     * @brief Get the OBD PID Signal decoder format
     * @param signalID the unique signalID
     * @return invalid Decoder format if signal is not OBD PID signal
     */
    virtual PIDSignalDecoderFormat getPIDSignalDecoderFormat( SignalID signalID ) const = 0;

    /**
     * @brief Used by the AWS IoT MQTT callback to copy data received from Cloud into this object without any further
     * processing to minimize time spent in callback context.
     *
     * @param inputBuffer Byte array of raw protobuffer data for a decoder_manifest.proto type binary blob
     * @param size Size of the data buffer
     *
     * @return True if successfully copied, false if failure to copy data.
     */
    virtual bool copyData( const std::uint8_t *inputBuffer, const size_t size ) = 0;

    /**
     * @brief This function returns mProtoBinaryData majorly used for persistent
     * storage
     *
     * @return binary data in a vector
     */
    virtual const std::vector<uint8_t> &getData() const = 0;

    /**
     * @brief Virtual destructor to be implemented by the base class
     */
    virtual ~IDecoderManifest() = 0;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
