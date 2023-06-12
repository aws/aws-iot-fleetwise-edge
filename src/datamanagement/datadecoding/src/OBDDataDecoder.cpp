// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "OBDDataDecoder.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <algorithm>
#include <cmath>
#include <ios>
#include <sstream>
constexpr int POSITIVE_ECU_RESPONSE_BASE = 0x40;
#define IS_BIT_SET( var, pos ) ( ( ( var ) & static_cast<uint8_t>( 1U << ( pos ) ) ) != 0U )

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

OBDDataDecoder::OBDDataDecoder( std::shared_ptr<OBDDecoderDictionary> &decoderDictionary )
    : mDecoderDictionary{ decoderDictionary }
{
    mTimer.reset();
}

bool
OBDDataDecoder::decodeSupportedPIDs( const SID sid,
                                     const std::vector<uint8_t> &inputData,
                                     SupportedPIDs &supportedPIDs )
{
    // First look at whether we received a positive response
    // The positive response can be identified by 0x40 + SID.
    // If the input size is less than 6 ( Response byte + Requested PID + 4 data bytes )
    // or if the input size minus SID is not a multiple of 5
    // this is also not a valid input
    if ( ( inputData.size() < 6 ) || ( POSITIVE_ECU_RESPONSE_BASE + toUType( sid ) != inputData[0] ) ||
         ( ( inputData.size() - 1 ) % 5 != 0 ) )
    {
        FWE_LOG_WARN( "Invalid Supported PID Input" );
        return false;
    }
    // Make sure we put only the ones we support by our software in the result.
    // The reason for that is if we request something we don't support yet
    // we cannot parse the response correctly i.e. number of bytes per PID is NOT fixed
    // The structure of a positive response should be according to :
    // Section 8.1.2.2 Request Current Powertrain Diagnostic Data Response Message Definition (report supported PIDs)
    // from the J1979 spec
    // 0x41(Positive response), 0x00( requested PID range), 4 Bytes, 0x20( requested PID range), 4 Bytes. etc
    // basePID is the requested PID range such as 0x00, 0x20
    PID basePID = 0;
    // baseIdx is the byte index for the base PID. e.g: 1 for 0x00, 6 for 0x20.
    size_t baseIdx = 0;
    for ( size_t i = 1; i < inputData.size(); ++i )
    {
        // First extract the PID Range, its position is always ByteIndex mod 5
        if ( ( i % 5 ) == 1 )
        {
            // if the PID is 0x00, the basePID is 0x00 and baseIdx is 1
            // if the PID is 0x20, the basePID is 0x00 and baseIdx is 6
            basePID = inputData[i];
            if ( basePID % SUPPORTED_PID_STEP != 0 )
            {
                FWE_LOG_WARN( "Invalid PID for support range: " + std::to_string( basePID ) );
                break;
            }
            baseIdx = i;
            // Skip this byte
            continue;
        }
        for ( size_t j = 0; j < BYTE_SIZE; ++j )
        {
            if ( IS_BIT_SET( inputData[i], j ) )
            {
                // E.g. if MSb in the first byte after 0x20 is set, then
                // i = 7, baseIdx = 6, and j = 7, put SID1_PID_33 in the result
                size_t index = ( i - baseIdx ) * BYTE_SIZE - j + basePID;
                PID decodedID = getPID( sid, index );
                // The response includes the PID range requested.
                // To remain consistent with the spec, we don't want to mix Supported PID IDs with
                // PIDs. We validate that the PID received is supported by the software, but also
                // exclude the Supported PID IDs from the output list
                if ( ( decodedID != INVALID_PID ) &&
                     ( std::find( supportedPIDRange.begin(), supportedPIDRange.end(), decodedID ) ==
                       supportedPIDRange.end() ) )
                {
                    supportedPIDs.emplace_back( decodedID );
                }
            }
        }
    }
    // Sort the result for easy lookup
    std::sort( supportedPIDs.begin(), supportedPIDs.end() );

    return !supportedPIDs.empty();
}

bool
OBDDataDecoder::decodeEmissionPIDs( const SID sid,
                                    const std::vector<PID> &pids,
                                    const std::vector<uint8_t> &inputData,
                                    EmissionInfo &info )
{
    // First look at whether we received a positive response
    // The positive response can be identified by 0x40 + SID.
    // If the input size is less than 3 ( Positive Response + Response byte + Requested PID )

    // this is also not a valid input as we expect at least one by response.
    if ( ( inputData.size() < 3 ) || ( POSITIVE_ECU_RESPONSE_BASE + toUType( sid ) != inputData[0] ) )
    {
        FWE_LOG_WARN( "Invalid response to PID request" );
        return false;
    }
    if ( mDecoderDictionary == nullptr )
    {
        FWE_LOG_WARN( "Invalid Decoder Dictionary" );
        return false;
    }
    // Validate 1) The PIDs in response match with expected PID; 2) Total length of PID response matches with Decoder
    // Manifest. If not matched, the program will discard this response and not attempt to decode.
    if ( isPIDResponseValid( pids, inputData ) == false )
    {
        FWE_LOG_WARN( "Invalid PIDs response" );
        return false;
    }
    // Setup the Info
    info.mSID = sid;
    // Start from byte number 2 which is the PID requested
    size_t byteCounter = 1;
    while ( byteCounter < inputData.size() )
    {
        auto pid = inputData[byteCounter];
        byteCounter++;
        // first check whether the decoder dictionary contains this PID
        if ( mDecoderDictionary->find( pid ) != mDecoderDictionary->end() )
        {
            // The expected number of bytes returned from PID
            auto expectedResponseLength = mDecoderDictionary->at( pid ).mSizeInBytes;
            auto formulas = mDecoderDictionary->at( pid ).mSignals;
            // first check whether we have received enough bytes for this PID
            if ( byteCounter + expectedResponseLength <= inputData.size() )
            {
                // check how many signals do we need to collect from this PID.
                // This is defined in cloud decoder manifest.
                // Each signal has its associated formula
                for ( auto formula : formulas )
                {
                    calculateValueFromFormula( pid, formula, inputData, byteCounter, info );
                }
            }
            // Done with this PID, move on to next PID by increment byteCounter by response length of current PID
            byteCounter += expectedResponseLength;
        }
        else
        {
            FWE_LOG_TRACE( "PID " + std::to_string( pid ) + " missing in decoder dictionary" );
            // Cannot decode this byte as it doesn't exist in both decoder dictionary
            // Cannot proceed with the rest of response because the payload might already be misaligned.
            // Note because we already checked the response validity with isPIDResponseValid(), the program should
            // not come here logically. But it might still happen in rare case such as bit flipping.
            break;
        }
    }
    return !info.mPIDsToValues.empty();
}

bool
OBDDataDecoder::decodeDTCs( const SID sid, const std::vector<uint8_t> &inputData, DTCInfo &info )
{
    // First look at whether we received a positive response
    // The positive response can be identified by 0x40 + SID.
    // If an ECU has no DTCs, it should respond with 2 Bytes ( 1 for Positive response + 1 number of DTCs( 0) )
    if ( ( inputData.size() < 2 ) || ( POSITIVE_ECU_RESPONSE_BASE + toUType( sid ) != inputData[0] ) )
    {
        return false;
    }
    info.mSID = sid;
    // First Byte is the DTC count
    size_t dtcCount = inputData[1];
    // The next bytes are expected to be the actual DTCs.
    if ( dtcCount == 0 )
    {
        // No DTC reported, all good
        return true;
    }
    else
    {
        // Expect the size of the ECU response to be 2 + the 2 bytes for each DTC
        if ( ( dtcCount * 2 ) + 2 != inputData.size() )
        {
            // Corrupt frame
            return false;
        }
        // Process the DTCs in a chunk of 2 bytes
        std::string dtcString;
        for ( size_t byteIndex = 2; byteIndex < inputData.size() - 1; byteIndex += 2 )
        {

            if ( extractDTCString( inputData[byteIndex], inputData[byteIndex + 1], dtcString ) )
            {
                info.mDTCCodes.emplace_back( dtcString );
            }
        }
    }

    return !info.mDTCCodes.empty();
}

bool
OBDDataDecoder::extractDTCString( const uint8_t &firstByte, const uint8_t &secondByte, std::string &dtcString )
{
    dtcString.clear();
    std::stringstream stream;
    // Decode the DTC Domain according to J1979 8.3.1
    // Extract the first 2 bits of the first Byte
    switch ( firstByte >> 6 )
    {
    case toUType( DTCDomains::POWERTRAIN ): // Powertrain
        stream << 'P';
        break;
    case toUType( DTCDomains::CHASSIS ): // Powertrain
        stream << 'C';
        break;
    case toUType( DTCDomains::BODY ): // Powertrain
        stream << 'B';
        break;
    case toUType( DTCDomains::NETWORK ): // Powertrain
        stream << 'U';
        break;
    default:
        break;
    }

    // Extract the first digit of the DTC ( second 2 bits from first byte)
    stream << std::hex << ( ( firstByte & 0x30 ) >> 4 );
    // Next digit is the last 4 bits of the first byte
    stream << std::hex << ( firstByte & 0x0F );
    // Next digit is the first 4 bits of the second byte
    stream << std::hex << ( secondByte >> 4 );
    // Next digit is the last 4 bits of the second byte
    stream << std::hex << ( secondByte & 0x0F );
    dtcString = stream.str();
    // Apply upper case before returning
    std::transform( dtcString.begin(), dtcString.end(), dtcString.begin(), ::toupper );
    return !dtcString.empty();
}

bool
OBDDataDecoder::decodeVIN( const std::vector<uint8_t> &inputData, std::string &vin )
{
    // First look at whether we received a positive response
    // The positive response can be identified by 0x40 + SID.
    // The response is usually 1 byte of the positive response, 1 byte for the InfoType(PID), 1 byte for the number of
    // data item.
    if ( ( inputData.size() < 3 ) ||
         ( POSITIVE_ECU_RESPONSE_BASE + toUType( vehicleIdentificationNumberRequest.mSID ) != inputData[0] ) ||
         ( vehicleIdentificationNumberRequest.mPID != inputData[1] ) )
    {
        return false;
    }
    // Assign the rest of the vector to the output string
    vin.assign( inputData.begin() + 3, inputData.end() );
    return !vin.empty();
}

bool
OBDDataDecoder::isPIDResponseValid( const std::vector<PID> &pids, const std::vector<uint8_t> &ecuResponse )
{
    // All PIDs which are still expected in the remaining message
    // once processed they are set to INVALID_PID
    std::vector<PID> pidsLeft( pids );

    // This index is used to iterate through the ECU PID response length
    // As the first byte in response is the Service Mode, we will start from the second byte.
    size_t responseByteIndex = 1;
    while ( responseByteIndex < ecuResponse.size() )
    {
        auto pid = ecuResponse[responseByteIndex];
        auto foundPid = std::find( pidsLeft.begin(), pidsLeft.end(), pid );
        if ( foundPid == pidsLeft.end() )
        {
            FWE_LOG_WARN( "PID " + std::to_string( pid ) + " in ECU response position " +
                          std::to_string( responseByteIndex ) + " is not expected" );
            return false;
        }
        *foundPid = INVALID_PID; // for every time a PID is requested only one response is expected
        if ( mDecoderDictionary->find( pid ) != mDecoderDictionary->end() )
        {
            // Move Index into the next PID
            responseByteIndex += ( mDecoderDictionary->at( pid ).mSizeInBytes + 1 );
        }
        else
        {
            FWE_LOG_WARN( "PID " + std::to_string( pid ) + " not found in decoder dictionary" );
            return false;
        }
    }

    for ( auto p : pidsLeft )
    {
        if ( p != INVALID_PID )
        {
            FWE_LOG_TRACE( "Cannot find PID " + std::to_string( p ) + " which was requested in ECU response" );
        }
    }
    if ( responseByteIndex != ecuResponse.size() )
    {
        FWE_LOG_WARN( "Expect response length: " + std::to_string( responseByteIndex ) +
                      " Actual response length: " + std::to_string( ecuResponse.size() ) );
    }
    return responseByteIndex == ecuResponse.size();
}

void
OBDDataDecoder::calculateValueFromFormula( PID pid,
                                           const CANSignalFormat &formula,
                                           const std::vector<uint8_t> &inputData,
                                           size_t byteCounter,
                                           EmissionInfo &info )
{
    // Before using formula, check it against rule
    if ( !isFormulaValid( pid, formula ) )
    {
        return;
    }

    // In J1979 spec, longest value has 4-byte.
    // Allocate 64-bit here in case signal value increased in the future.
    // Currently we always consider the raw value is positive. There are cases where a raw
    // value itself is a negative integer as 2-complement. In those cases, the raw value will also
    // be interpreted as positive because we store it in a 64-bit type without looking at the sign.
    // In the future we have to know whether a raw value is signed and then extend the sign (filling
    // all left-side bits with 1 instead of 0).
    uint64_t rawData = 0;
    size_t byteIdx = byteCounter + ( formula.mFirstBitPosition / BYTE_SIZE );
    // If the signal length is less than 8-bit, we need to perform bit field operation
    if ( formula.mSizeInBits < BYTE_SIZE )
    {
        // bit manipulation performed here: shift first, then apply mask
        // e.g. If signal are bit 4 ~ 7 in Byte A.
        // we firstly right shift by 4, then apply bit mask 0b1111
        rawData = inputData[byteIdx];
        rawData >>= formula.mFirstBitPosition % BYTE_SIZE;
        rawData &= static_cast<uint64_t>( 0xFFULL ) >> ( BYTE_SIZE - formula.mSizeInBits );
    }
    else
    {
        // This signal contain greater or equal than one byte, concatenate raw bytes
        auto numOfBytes = formula.mSizeInBits / BYTE_SIZE;
        // This signal contains multiple bytes, concatenate the bytes
        while ( numOfBytes != 0 )
        {
            --numOfBytes;
            rawData = ( rawData << BYTE_SIZE ) | static_cast<uint64_t>( inputData[byteIdx] );
            byteIdx++;
        }
    }

    const auto signalType = formula.mSignalType;
    switch ( signalType )
    {
    case ( SignalType::UINT64 ): {
        uint64_t calculatedValue = 0;
        if ( ( formula.mFactor > 0.0 ) && ( floor( formula.mFactor ) == formula.mFactor ) &&
             ( floor( formula.mOffset ) == formula.mOffset ) )
        {
            if ( formula.mOffset >= 0.0 )
            {
                calculatedValue = static_cast<uint64_t>( rawData ) * static_cast<uint64_t>( formula.mFactor ) +
                                  static_cast<uint64_t>( formula.mOffset );
            }
            else
            {
                calculatedValue = static_cast<uint64_t>( rawData ) * static_cast<uint64_t>( formula.mFactor ) -
                                  static_cast<uint64_t>( std::abs( formula.mOffset ) );
            }
        }
        else
        {
            calculatedValue =
                static_cast<uint64_t>( static_cast<double>( rawData ) * formula.mFactor + formula.mOffset );
            TraceModule::get().incrementVariable( TraceVariable::OBD_POSSIBLE_PRECISION_LOSS_UINT64 );
        }
        info.mPIDsToValues.emplace( formula.mSignalID, OBDSignal( calculatedValue, signalType ) );
        break;
    }
    case ( SignalType::INT64 ): {
        int64_t calculatedValue = 0;
        if ( ( floor( formula.mFactor ) == formula.mFactor ) && ( floor( formula.mOffset ) == formula.mOffset ) )
        {
            calculatedValue = static_cast<int64_t>( rawData ) * static_cast<int64_t>( formula.mFactor ) +
                              static_cast<int64_t>( formula.mOffset );
        }
        else
        {
            calculatedValue =
                static_cast<int64_t>( static_cast<double>( rawData ) * formula.mFactor + formula.mOffset );
            TraceModule::get().incrementVariable( TraceVariable::OBD_POSSIBLE_PRECISION_LOSS_INT64 );
        }
        info.mPIDsToValues.emplace( formula.mSignalID, OBDSignal( calculatedValue, signalType ) );
        break;
    }
    // For any other type, we can safely cast everything to double as only int64 and uint64 can't
    // fit in a double.
    default: {
        double calculatedValue =
            static_cast<double>( static_cast<int64_t>( rawData ) ) * formula.mFactor + formula.mOffset;
        info.mPIDsToValues.emplace( formula.mSignalID, OBDSignal( calculatedValue, signalType ) );
    }
    }
}

bool
OBDDataDecoder::isFormulaValid( PID pid, const CANSignalFormat &formula )
{
    bool isValid = false;
    // Here's the rules we apply to check whether PID formula is valid
    // 1. First Bit Position has to be less than last bit position of PID response length
    // 2. Last Bit Position (first bit + sizeInBits) has to be less than or equal to last bit position of PID response
    // length
    // 3. If mSizeInBits are greater or equal than 8, both mSizeInBits and first bit position has to be multiple of 8
    if ( ( mDecoderDictionary->find( pid ) != mDecoderDictionary->end() ) &&
         ( formula.mFirstBitPosition < mDecoderDictionary->at( pid ).mSizeInBytes * BYTE_SIZE ) &&
         ( formula.mSizeInBits + formula.mFirstBitPosition <=
           mDecoderDictionary->at( pid ).mSizeInBytes * BYTE_SIZE ) &&
         ( ( formula.mSizeInBits < 8 ) ||
           ( ( ( formula.mSizeInBits & 0x7 ) == 0 ) && ( ( formula.mFirstBitPosition & 0x7 ) == 0 ) ) ) )
    {
        isValid = true;
    }
    return isValid;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
