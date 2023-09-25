// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "EventTypes.h"
#include "MessageTypes.h"
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include <cstdint>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

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
    PIDSignalDecoderFormat() = default;

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
    size_t mPidResponseLength{ 0 };

    /*
     * OBDII-PID Service Mode for the signal in decimal
     */
    SID mServiceMode{ SID::INVALID_SERVICE_MODE };

    /*
     * OBD request PID in decimal
     */
    PID mPID{ 0 };

    /*
     * scaling to decode OBD from raw bytes to double value
     * e.g. A * 0.0125 - 40. scaling is 0.01
     */
    double mScaling{ 0 };

    /*
     * offset to decode OBD from raw bytes to double value
     * e.g. A * 0.0125 - 40. offset is -40.0
     */
    double mOffset{ 0 };

    /*
     * the start byte order (starting from 0th) for this signal in its PID query response
     * e.g. PID 0x14 contains two signals. SHRFT is the second byte. Its startByte is 1
     */
    size_t mStartByte{ 0 };

    /*
     * number of bytes for this signal in its PID query response
     * e.g. PID 0x14 contains two signals. SHRFT is one byte. Its byteLength is 1
     */
    size_t mByteLength{ 0 };

    /*
     * Right shift on bits to decode this signal from raw bytes. Note the bit manipulation is
     * only performed when byteLength is 1.
     * e.g. Boost Pressure B Control Status is bit 2, 3 on byte J. The right shift shall be 2
     *      For non-bitmask signals, the right shift shall always be 0
     */
    uint8_t mBitRightShift{ 0 };

    /*
     * bit Mask Length to be applied to decode this signal from raw byte. Note the bit manipulation
     * is only performed when byteLength is 1.
     * e.g. Boost Pressure B Control Status is bit 2, 3 on byte J. The bit Mask Length would be 2
     *      For non-bitmask signals, the bit Mask Length shall always be 8.
     */
    uint8_t mBitMaskLength{ 0 };

    /**
     * @brief The datatype of the signal. The default is double for backward compatibility
     */
    SignalType mSignalType{ SignalType::DOUBLE };

public:
    /**
     * @brief Overload of the == operator
     * @param other Other PIDSignalDecoderFormat object to compare
     * @return true if ==, false otherwise
     */
    bool
    operator==( const PIDSignalDecoderFormat &other ) const
    {
        return ( mPidResponseLength == other.mPidResponseLength ) && ( mServiceMode == other.mServiceMode ) &&
               ( mPID == other.mPID ) && ( mScaling == other.mScaling ) && ( mOffset == other.mOffset ) &&
               ( mStartByte == other.mStartByte ) && ( mByteLength == other.mByteLength ) &&
               ( mBitRightShift == other.mBitRightShift ) && ( mBitMaskLength == other.mBitMaskLength );
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
     * @brief Get the Vehicle Data Source Protocol for this Signal
     * @param signalID the unique signalID
     * @return invalid Protocol Type if signal is not found in decoder manifest
     */
    virtual VehicleDataSourceProtocol getNetworkProtocol( SignalID signalID ) const = 0;

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
     * @brief This function returns Signal Type from the Decoder
     *
     * @param signalID
     * @return SignalType
     */
    virtual SignalType getSignalType( const SignalID signalID ) const = 0;

    /**
     * @brief Virtual destructor to be implemented by the base class
     */
    virtual ~IDecoderManifest() = default;
};

} // namespace IoTFleetWise
} // namespace Aws
