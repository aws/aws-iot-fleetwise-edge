// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CANDecoder.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <typeinfo>

namespace Aws
{
namespace IoTFleetWise
{

bool
CANDecoder::decodeCANMessage( const uint8_t *frameData,
                              size_t frameSize,
                              const CANMessageFormat &format,
                              const std::unordered_set<uint32_t> &signalIDsToCollect,
                              std::vector<CANDecodedSignal> &decodedSignals )
{
    uint8_t errorCounter = 0;
    uint32_t frameSizeInBits = static_cast<uint32_t>( frameSize * 8 );
    uint8_t multiplexorValue = UINT8_MAX;

    // Check if the message is multiplexed
    if ( format.isMultiplexed() )
    {
        // Lookup the multiplexor signal
        // complexity. Try to fix in the scheme not in the code.
        auto it = std::find_if( format.mSignals.begin(), format.mSignals.end(), []( CANSignalFormat signal ) -> bool {
            return signal.isMultiplexor();
        } );
        if ( it == format.mSignals.end() )
        {
            FWE_LOG_ERROR( "Message ID" + std::to_string( format.mMessageID ) +
                           " is multiplexed but no Multiplexor signal has been found" );
            return false;
        }
        if ( signalIDsToCollect.find( it->mSignalID ) != signalIDsToCollect.end() )
        {
            // Decode the multiplexor Value
            int64_t rawValue = extractIntegerSignalFromFrame( frameData, *it );
            multiplexorValue = static_cast<uint8_t>( static_cast<uint8_t>( rawValue ) * it->mFactor + it->mOffset );

            const auto CANsignalType = it->mSignalType;
            switch ( CANsignalType )
            {
            case SignalType::UINT64: {
                auto physicalRawValue = static_cast<uint64_t>( multiplexorValue );
                auto physicalValue = DecodedSignalValue( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back( it->mSignalID, physicalValue, CANsignalType );
                break;
            }
            case SignalType::INT64: {

                auto physicalRawValue = static_cast<int64_t>( multiplexorValue );
                auto physicalValue = DecodedSignalValue( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back( it->mSignalID, physicalValue, CANsignalType );
                break;
            }
            default: {

                auto physicalRawValue = static_cast<double>( multiplexorValue );
                auto physicalValue = DecodedSignalValue( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back( it->mSignalID, physicalValue, CANsignalType );
                break;
            }
            }
        }
    }

    for ( size_t i = 0; i < format.mSignals.size(); ++i )
    {
        if ( signalIDsToCollect.find( format.mSignals[i].mSignalID ) != signalIDsToCollect.end() )
        {
            // Skip the signals that don't match the MUX value
            if ( ( multiplexorValue != UINT8_MAX ) && ( format.mSignals[i].mMultiplexorValue != multiplexorValue ) )
            {
                continue;
            }

            if ( ( format.mSignals[i].mFirstBitPosition >= frameSizeInBits ) ||
                 ( ( format.mSignals[i].mSizeInBits < 1 ) || ( format.mSignals[i].mSizeInBits > frameSizeInBits ) ) )
            {
                // Wrongly coded Signal, skip it
                FWE_LOG_ERROR( "Signal Out of Range" );
                errorCounter++;
                continue;
            }

            if ( ( !format.mSignals[i].mIsBigEndian ) &&
                 ( format.mSignals[i].mFirstBitPosition + format.mSignals[i].mSizeInBits > frameSizeInBits ) )
            {
                // Wrongly coded Signal, skip it
                FWE_LOG_ERROR( "Little endian signal Out of Range" );
                errorCounter++;
                continue;
            }

            switch ( format.mSignals[i].mRawSignalType )
            {
            case RawSignalType::INTEGER: {
                decodedSignals.emplace_back( decodeIntegerSignal( frameData, format.mSignals[i] ) );
                break;
            }
            case RawSignalType::FLOATING_POINT: {
                if ( ( format.mSignals[i].mSizeInBits != 32 ) && ( format.mSignals[i].mSizeInBits != 64 ) )
                {
                    FWE_LOG_ERROR( "Floating point signal must be 32 or 64 bits but got " +
                                   std::to_string( format.mSignals[i].mSizeInBits ) );
                    errorCounter++;
                    continue;
                }
                decodedSignals.emplace_back( decodeFloatingPointSignal( frameData, format.mSignals[i] ) );
                break;
            }
            }
        }
    }

    // Should not harm, callers will ignore the return code.
    return errorCounter == 0;
}

CANDecodedSignal
CANDecoder::decodeIntegerSignal( const uint8_t *frameData, const CANSignalFormat &signalDescription )
{
    const auto canSignalType = signalDescription.mSignalType;
    int64_t rawValue = extractIntegerSignalFromFrame( frameData, signalDescription );
    switch ( canSignalType )
    {
    case SignalType::UINT64: {
        if ( typeid( signalDescription.mFactor ) == typeid( double ) )
        {
            FWE_LOG_WARN( "Scaling Factor is double for signal ID " + std::to_string( signalDescription.mSignalID ) +
                          " and type as uint64" );
        }
        uint64_t physicalRawValue =
            static_cast<uint64_t>( rawValue ) * static_cast<uint64_t>( signalDescription.mFactor ) +
            static_cast<uint64_t>( signalDescription.mOffset );
        return { signalDescription.mSignalID, DecodedSignalValue( physicalRawValue, canSignalType ), canSignalType };
    }
    case SignalType::INT64: {
        auto physicalRawValue = static_cast<int64_t>( rawValue ) * static_cast<int64_t>( signalDescription.mFactor ) +
                                static_cast<int64_t>( signalDescription.mOffset );
        return { signalDescription.mSignalID, DecodedSignalValue( physicalRawValue, canSignalType ), canSignalType };
    }
    default: {
        auto physicalRawValue = static_cast<double>( rawValue ) * signalDescription.mFactor + signalDescription.mOffset;
        return { signalDescription.mSignalID, DecodedSignalValue( physicalRawValue, canSignalType ), canSignalType };
    }
    }
}

CANDecodedSignal
CANDecoder::decodeFloatingPointSignal( const uint8_t *frameData, const CANSignalFormat &signalDescription )
{
    const auto canSignalType = signalDescription.mSignalType;
    if ( signalDescription.mSizeInBits == 32 )
    {
        uint32_t rawValue = extractRawSignalFromFrame<uint32_t>( frameData, signalDescription );
        float *floatValue = reinterpret_cast<float *>( &rawValue );
        auto physicalRawValue =
            static_cast<double>( *floatValue ) * signalDescription.mFactor + signalDescription.mOffset;
        return { signalDescription.mSignalID, DecodedSignalValue( physicalRawValue, canSignalType ), canSignalType };
    }
    else
    {
        uint64_t rawValue = extractRawSignalFromFrame<uint64_t>( frameData, signalDescription );
        double *doubleValue = reinterpret_cast<double *>( &rawValue );
        auto physicalRawValue =
            static_cast<double>( *doubleValue ) * signalDescription.mFactor + signalDescription.mOffset;
        return { signalDescription.mSignalID, DecodedSignalValue( physicalRawValue, canSignalType ), canSignalType };
    }
}

int64_t
CANDecoder::extractIntegerSignalFromFrame( const uint8_t *frameData, const CANSignalFormat &signalDescription )
{
    uint64_t rawValue = extractRawSignalFromFrame<uint64_t>( frameData, signalDescription );

    // perform sign extension
    if ( signalDescription.mIsSigned )
    {
        uint64_t msbSignMask = static_cast<uint64_t>( 1U ) << ( signalDescription.mSizeInBits - 1 );
        rawValue = ( ( rawValue ^ msbSignMask ) - msbSignMask );
    }
    return static_cast<int64_t>( rawValue );
}

template <typename T>
auto
CANDecoder::extractRawSignalFromFrame( const uint8_t *frameData, const CANSignalFormat &signalDescription ) -> T
{
    uint16_t startBit = static_cast<uint16_t>( signalDescription.mFirstBitPosition );
    uint8_t startByte = static_cast<uint8_t>( startBit / BYTE_SIZE );
    uint8_t startBitInByte = startBit % BYTE_SIZE;
    uint8_t resultLength = static_cast<uint8_t>( BYTE_SIZE - startBitInByte );
    uint8_t endByte = 0U;

    // Write first bits to result
    // NOTE: The start bit here is different from how it appears in a DBC file. In a DBC file, the
    // start bit indicates the LSB for little endian and MSB for big endian signals.
    // But AWS IoT FleetWise considers start bit to always be the LSB regardless of endianess.
    T result = frameData[startByte] >> startBitInByte;

    // Write residual bytes
    if ( signalDescription.mIsBigEndian ) // Motorola (big endian)
    {
        endByte = static_cast<uint8_t>(
            ( startByte * BYTE_SIZE + BYTE_SIZE - startBitInByte - signalDescription.mSizeInBits ) / BYTE_SIZE );

        for ( int count = startByte - 1; count >= endByte; count-- )
        {
            result |= static_cast<T>( frameData[count] ) << resultLength;
            resultLength = static_cast<uint8_t>( resultLength + BYTE_SIZE );
        }
    }
    else // Intel (little endian)
    {
        endByte = static_cast<uint8_t>( ( startBit + signalDescription.mSizeInBits - 1 ) / BYTE_SIZE );

        for ( int count = startByte + 1; count <= endByte; count++ )
        {
            result |= static_cast<T>( frameData[count] ) << resultLength;
            resultLength = static_cast<uint8_t>( resultLength + BYTE_SIZE );
        }
    }

    // Mask value
    auto mask =
        ( static_cast<T>( 0xFFFFFFFFFFFFFFFFULL ) >> ( sizeof( T ) * BYTE_SIZE - ( signalDescription.mSizeInBits ) ) );
    result &= mask;

    return result;
}

} // namespace IoTFleetWise
} // namespace Aws
