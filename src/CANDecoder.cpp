// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CANDecoder.h"
#include "LoggingModule.h"
#include <algorithm>
#include <string>
#include <typeinfo>

#define MASK64( nbits ) ( static_cast<uint64_t>( 0xFFFFFFFFFFFFFFFFULL ) >> ( 64 - ( nbits ) ) )

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
            int64_t rawValue = extractSignalFromFrame( frameData, *it );
            multiplexorValue = static_cast<uint8_t>( static_cast<uint8_t>( rawValue ) * it->mFactor + it->mOffset );

            const auto CANsignalType = it->mSignalType;
            switch ( CANsignalType )
            {
            case ( SignalType::UINT64 ): {
                if ( typeid( it->mFactor ) == typeid( double ) )
                {
                    FWE_LOG_WARN( "Scaling Factor is double for signal ID " + std::to_string( it->mSignalID ) +
                                  " and type as uint64" );
                }
                auto physicalRawValue = static_cast<uint64_t>( multiplexorValue );
                auto physicalValue = CANPhysicalValueType( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back(
                    CANDecodedSignal( it->mSignalID, rawValue, physicalValue, CANsignalType ) );
                break;
            }
            case ( SignalType::INT64 ): {

                auto physicalRawValue = static_cast<int64_t>( multiplexorValue );
                auto physicalValue = CANPhysicalValueType( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back(
                    CANDecodedSignal( it->mSignalID, rawValue, physicalValue, CANsignalType ) );
                break;
            }
            default: {

                auto physicalRawValue = static_cast<double>( multiplexorValue );
                auto physicalValue = CANPhysicalValueType( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back(
                    CANDecodedSignal( it->mSignalID, rawValue, physicalValue, CANsignalType ) );
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

            // Start decoding the signal, extract the value before scaling from the Frame.
            int64_t rawValue = extractSignalFromFrame( frameData, format.mSignals[i] );
            const auto CANsignalType = format.mSignals[i].mSignalType;
            switch ( CANsignalType )
            {
            case ( SignalType::UINT64 ): {
                if ( typeid( format.mSignals[i].mFactor ) == typeid( double ) )
                {
                    FWE_LOG_WARN( "Scaling Factor is double for signal ID " +
                                  std::to_string( format.mSignals[i].mSignalID ) + " and type as uint64" );
                }
                uint64_t physicalRawValue =
                    static_cast<uint64_t>( rawValue ) * static_cast<uint64_t>( format.mSignals[i].mFactor ) +
                    static_cast<uint64_t>( format.mSignals[i].mOffset );
                auto physicalValue = CANPhysicalValueType( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back(
                    CANDecodedSignal( format.mSignals[i].mSignalID, rawValue, physicalValue, CANsignalType ) );
                break;
            }
            case ( SignalType::INT64 ): {
                auto physicalRawValue =
                    static_cast<int64_t>( rawValue ) * static_cast<int64_t>( format.mSignals[i].mFactor ) +
                    static_cast<int64_t>( format.mSignals[i].mOffset );
                auto physicalValue = CANPhysicalValueType( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back(
                    CANDecodedSignal( format.mSignals[i].mSignalID, rawValue, physicalValue, CANsignalType ) );
                break;
            }
            default: {
                auto physicalRawValue =
                    static_cast<double>( rawValue ) * format.mSignals[i].mFactor + format.mSignals[i].mOffset;
                auto physicalValue = CANPhysicalValueType( physicalRawValue, CANsignalType );
                decodedSignals.emplace_back(
                    CANDecodedSignal( format.mSignals[i].mSignalID, rawValue, physicalValue, CANsignalType ) );
            }
            }
        }
    }

    // Should not harm, callers will ignore the return code.
    return errorCounter == 0;
}

int64_t
CANDecoder::extractSignalFromFrame( const uint8_t *frameData, const CANSignalFormat &signalDescription )
{
    uint16_t startBit = static_cast<uint16_t>( signalDescription.mFirstBitPosition );
    uint8_t startByte = static_cast<uint8_t>( startBit / BYTE_SIZE );
    uint8_t startBitInByte = startBit % BYTE_SIZE;
    uint8_t resultLength = static_cast<uint8_t>( BYTE_SIZE - startBitInByte );
    uint8_t endByte = 0U;

    // Write first bits to result
    // NOTE: The start bit here is different from how it appears in a DBC file. In a DBC file, the
    // start bit indicates the LSB for little endian and MSB for big endian signals.
    // But AWS IoT Fleetwise considers start bit to always be the LSB regardless of endianess.
    uint64_t result = frameData[startByte] >> startBitInByte;

    // Write residual bytes
    if ( signalDescription.mIsBigEndian ) // Motorola (big endian)
    {
        endByte = static_cast<uint8_t>(
            ( startByte * BYTE_SIZE + BYTE_SIZE - startBitInByte - signalDescription.mSizeInBits ) / BYTE_SIZE );

        for ( int count = startByte - 1; count >= endByte; count-- )
        {
            result |= static_cast<uint64_t>( frameData[count] ) << resultLength;
            resultLength = static_cast<uint8_t>( resultLength + BYTE_SIZE );
        }
    }
    else // Intel (little endian)
    {
        endByte = static_cast<uint8_t>( ( startBit + signalDescription.mSizeInBits - 1 ) / BYTE_SIZE );

        for ( int count = startByte + 1; count <= endByte; count++ )
        {
            result |= static_cast<uint64_t>( frameData[count] ) << resultLength;
            resultLength = static_cast<uint8_t>( resultLength + BYTE_SIZE );
        }
    }

    // Mask value
    result &= MASK64( signalDescription.mSizeInBits );

    // perform sign extension
    if ( signalDescription.mIsSigned )
    {
        uint64_t msbSignMask = static_cast<uint64_t>( 1U ) << ( signalDescription.mSizeInBits - 1 );
        result = ( ( result ^ msbSignMask ) - msbSignMask );
    }
    return static_cast<int64_t>( result );
}

} // namespace IoTFleetWise
} // namespace Aws
