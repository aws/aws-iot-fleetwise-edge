
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OBDDataDecoder.h"
#include "EnumUtility.h"
#include "Testing.h"
#include "datatypes/OBDDataTypesUnitTestOnly.h"
#include <gtest/gtest.h>
#include <unistd.h>

using namespace Aws::IoTFleetWise::DataManagement;
using namespace Aws::IoTFleetWise::TestingSupport;

// For testing purpose, Signal ID is defined as PID | (signal_order << PID_SIGNAL_BITS_LEFT_SHIFT)
#define PID_SIGNAL_BITS_LEFT_SHIFT 8

class OBDDataDecoderTest : public ::testing::Test
{
protected:
    std::shared_ptr<OBDDecoderDictionary> decoderDictPtr;
    OBDDataDecoder decoder{ decoderDictPtr };
    void
    SetUp() override
    {
        decoderDictPtr = std::make_shared<OBDDecoderDictionary>();
        // In final product, signal ID comes from Cloud, edge doesn't generate signal ID
        // Below signal ID initialization got implemented only for Edge testing.
        // In this test, signal ID are defined as PID | (signal order) << 8
        for ( PID pid = toUType( EmissionPIDs::FUEL_SYSTEM_STATUS ); pid <= toUType( EmissionPIDs::ODOMETER ); ++pid )
        {
            // Initialize decoder dictionary based on mode1PIDs table from OBDDataDecoder module
            // Note in actual product, the decoder dictionary comes from decoder manifest
            CANMessageFormat format;
            format.mMessageID = pid;
            format.mSizeInBytes = static_cast<uint8_t>( mode1PIDs[pid].retLen );
            format.mSignals = std::vector<CANSignalFormat>( mode1PIDs[pid].formulas.size() );
            for ( uint32_t idx = 0; idx < mode1PIDs[pid].formulas.size(); ++idx )
            {
                format.mSignals[idx].mSignalID = pid | ( idx << PID_SIGNAL_BITS_LEFT_SHIFT );
                format.mSignals[idx].mFirstBitPosition = static_cast<uint16_t>(
                    mode1PIDs[pid].formulas[idx].byteOffset * BYTE_SIZE + mode1PIDs[pid].formulas[idx].bitShift );
                format.mSignals[idx].mSizeInBits =
                    static_cast<uint16_t>( ( mode1PIDs[pid].formulas[idx].numOfBytes - 1 ) * BYTE_SIZE +
                                           mode1PIDs[pid].formulas[idx].bitMaskLen );
                format.mSignals[idx].mFactor = mode1PIDs[pid].formulas[idx].scaling;
                format.mSignals[idx].mOffset = mode1PIDs[pid].formulas[idx].offset;
            }
            decoderDictPtr->emplace( pid, format );
        }
    }

    void
    assertSignalValue( const OBDSignal &obdSignal, double expectedSignalValue, SignalType expectedSignalType )
    {
        switch ( expectedSignalType )
        {
        case SignalType::UINT8:
            ASSERT_EQ( static_cast<uint8_t>( obdSignal.signalValue.doubleVal ),
                       static_cast<uint8_t>( expectedSignalValue ) );
            break;
        case SignalType::INT8:
            ASSERT_EQ( static_cast<int8_t>( obdSignal.signalValue.doubleVal ),
                       static_cast<int8_t>( expectedSignalValue ) );
            break;
        case SignalType::UINT16:
            ASSERT_EQ( static_cast<uint16_t>( obdSignal.signalValue.doubleVal ),
                       static_cast<uint16_t>( expectedSignalValue ) );
            break;
        case SignalType::INT16:
            ASSERT_EQ( static_cast<int16_t>( obdSignal.signalValue.doubleVal ),
                       static_cast<int16_t>( expectedSignalValue ) );
            break;
        case SignalType::UINT32:
            ASSERT_EQ( static_cast<uint32_t>( obdSignal.signalValue.doubleVal ),
                       static_cast<uint32_t>( expectedSignalValue ) );
            break;
        case SignalType::INT32:
            ASSERT_EQ( static_cast<int32_t>( obdSignal.signalValue.doubleVal ),
                       static_cast<int32_t>( expectedSignalValue ) );
            break;
        case SignalType::UINT64:
            ASSERT_EQ( obdSignal.signalValue.uint64Val, static_cast<uint64_t>( expectedSignalValue ) );
            break;
        case SignalType::INT64:
            ASSERT_EQ( obdSignal.signalValue.int64Val, static_cast<int64_t>( expectedSignalValue ) );
            break;
        case SignalType::FLOAT:
            ASSERT_FLOAT_EQ( static_cast<float>( obdSignal.signalValue.doubleVal ),
                             static_cast<float>( expectedSignalValue ) );
            break;
        case SignalType::DOUBLE:
            ASSERT_DOUBLE_EQ( obdSignal.signalValue.doubleVal, static_cast<double>( expectedSignalValue ) );
            break;
        case SignalType::BOOLEAN:
            ASSERT_EQ( static_cast<bool>( obdSignal.signalValue.doubleVal ), static_cast<bool>( expectedSignalValue ) );
            break;
        default:
            FAIL() << "Unsupported signal type";
        };
    }
};

class OBDDataDecoderTestWithAllSignalTypes : public OBDDataDecoderTest, public testing::WithParamInterface<SignalType>
{
};

INSTANTIATE_TEST_SUITE_P( AllSignals, OBDDataDecoderTestWithAllSignalTypes, allSignalTypes, signalTypeToString );

class OBDDataDecoderTestWithSignedSignalTypes : public OBDDataDecoderTest,
                                                public testing::WithParamInterface<SignalType>
{
};

INSTANTIATE_TEST_SUITE_P( SignedSignals,
                          OBDDataDecoderTestWithSignedSignalTypes,
                          signedSignalTypes,
                          signalTypeToString );

TEST_P( OBDDataDecoderTestWithAllSignalTypes, FullSingleByte )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 1;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 8;
    signalFormat.mFactor = (double)100 / 255;
    signalFormat.mOffset = 0;
    signalFormat.mSignalType = signalType;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x99 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 60, signalType );
}

TEST_P( OBDDataDecoderTestWithAllSignalTypes, FullSingleByteNegativeOffset )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 1;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 8;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = -10;
    signalFormat.mSignalType = signalType;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x99 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 143, signalType );
}

TEST_P( OBDDataDecoderTestWithAllSignalTypes, FullMultipleBytes )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 2;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 16;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = 0;
    signalFormat.mSignalType = signalType;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x00, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 10, signalType );
}

TEST_P( OBDDataDecoderTestWithAllSignalTypes, PartialByte )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 1;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 2;
    signalFormat.mSizeInBits = 2;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = 0;
    signalFormat.mSignalType = signalType;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    std::vector<uint8_t> txPDUData = { 0x41, pid, 0xFB };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 2, signalType );
}

TEST_P( OBDDataDecoderTestWithSignedSignalTypes, FullSingleByteWithUnsignedRawValue )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 1;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 8;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = 0;
    signalFormat.mSignalType = signalType;
    signalFormat.mIsSigned = false;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    std::vector<uint8_t> txPDUData = { 0x41, pid, 0xC4 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    // Even though the signal type is a signed type, the raw OBD value is not signed.
    // So the raw value 0xC4 should be interpreted as a positive integer instead of negative (-60).
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 196, signalType );
}

TEST_P( OBDDataDecoderTestWithSignedSignalTypes, FullMultipleBytesWithUnsignedRawValue )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 2;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 16;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = 0;
    signalFormat.mSignalType = signalType;
    signalFormat.mIsSigned = false;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    std::vector<uint8_t> txPDUData = { 0x41, pid, 0xC4, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    // Even though the signal type is a signed type, the raw OBD value is not signed.
    // So the raw value 0xC40A should be interpreted as a positive integer instead of negative (-15350).
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 50186, signalType );
}

TEST_P( OBDDataDecoderTestWithSignedSignalTypes, PartialByteWithUnsignedRawValue )
{
    SignalType signalType = GetParam();
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 1;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 2;
    signalFormat.mSizeInBits = 5;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = 0;
    signalFormat.mSignalType = signalType;
    signalFormat.mIsSigned = false;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    // 0x4C shifted 2 bits to the right (without extending the sign) = 0x13 = 19
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x4C };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    assertSignalValue( info.mPIDsToValues.at( signalFormat.mSignalID ), 19, signalType );
}

TEST_F( OBDDataDecoderTest, KeepPrecisionForUInt64 )
{
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 8;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 64;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = -100000;
    signalFormat.mSignalType = SignalType::UINT64;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    // 2305843009213693951, which when represented as a double loses precision
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    // Ensure that we are not casting to double anywhere
    ASSERT_EQ( info.mPIDsToValues.at( signalFormat.mSignalID ).signalValue.uint64Val, 2305843009213593951UL );
}

TEST_F( OBDDataDecoderTest, KeepPrecisionForInt64 )
{
    PID pid = 0xEF;
    CANMessageFormat format;
    format.mMessageID = pid;
    format.mSizeInBytes = 8;
    CANSignalFormat signalFormat;
    signalFormat.mSignalID = 0x100000EF;
    signalFormat.mFirstBitPosition = 0;
    signalFormat.mSizeInBits = 64;
    signalFormat.mFactor = 1.0;
    signalFormat.mOffset = -100000;
    signalFormat.mSignalType = SignalType::INT64;
    format.mSignals.emplace_back( signalFormat );
    decoderDictPtr->emplace( pid, format );

    // -6917529027641081857, which when represented as a double loses precision
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    // Ensure that we are not casting to double anywhere
    ASSERT_EQ( info.mPIDsToValues.at( signalFormat.mSignalID ).signalValue.int64Val, -6917529027641181857L );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedSupportedPIDs )
{
    // supported PID: 0x01 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    // 0x10, 0x11, 0x13, 0x15, 0x19, 0x1C, 0x21
    std::vector<uint8_t> txPDUData = { 0x41, 0x00, 0xBF, 0xBF, 0xA8, 0x91, 0x20, 0x80, 0x00, 0x00, 0x00 };
    SupportedPIDs pids;

    ASSERT_TRUE( decoder.decodeSupportedPIDs( SID::CURRENT_STATS, txPDUData, pids ) );
    // Expect only what we support in the SW
    // The PID list should NOT include the Supported PID ID that was requested i.e. 0X00
    std::vector<PID> expectedSupportedPIDs = { 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B, 0x0C,
                                               0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13, 0x15, 0x19, 0x1C, 0x21 };
    ASSERT_EQ( pids.size(), expectedSupportedPIDs.size() );
    for ( size_t idx = 0; idx < expectedSupportedPIDs.size(); ++idx )
    {
        ASSERT_EQ( pids[idx], expectedSupportedPIDs[idx] );
    }
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineLoad )
{
    // Engine load of 60 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x04, 0x99 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x04 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );

    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_LOAD ) ).signalValue.doubleVal, 60 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineTemperature )
{
    // Engine load of 70 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x05, 0x6E };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x05 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ) ).signalValue.doubleVal, 70 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedFuelTrim )
{
    // Fuel Trim on all banks of value 50
    // Raw Value is 192 -100 * 1.28
    std::vector<uint8_t> txPDUData = { 0x41, 0x06, 0xC0, 0x07, 0xC0, 0x08, 0xC0, 0x09, 0xC0 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x06, 0x07, 0x08, 0x09 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::SHORT_TERM_FUEL_TRIM_BANK_1 ) ).signalValue.doubleVal, 50 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::SHORT_TERM_FUEL_TRIM_BANK_2 ) ).signalValue.doubleVal, 50 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::LONG_TERM_FUEL_TRIM_BANK_1 ) ).signalValue.doubleVal, 50 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::LONG_TERM_FUEL_TRIM_BANK_2 ) ).signalValue.doubleVal, 50 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedIntakeManifoldPressure )
{
    // IntakeManifoldPressure of 200 pka
    std::vector<uint8_t> txPDUData = { 0x41, 0x0B, 0xC8 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x0B }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::INTAKE_MANIFOLD_ABSOLUTE_PRESSURE ) ).signalValue.doubleVal,
        200 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedIntakeAirFLowTemperature )
{
    // IntakeAirFLowTemperature of 30 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x0F, 0x46 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x0F }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::INTAKE_AIR_FLOW_TEMPERATURE ) ).signalValue.doubleVal, 30 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMAFRate )
{
    // MAFRate od 25 grams/sec
    std::vector<uint8_t> txPDUData = { 0x41, 0x10, 0x0A, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x10 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::MAF_RATE ) ).signalValue.doubleVal,
                      ( 256.0 * 0x0A + 0x0A ) / 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedThrottlePosition )
{
    // ThrottlePosition of 50 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x11, 0x80 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x11 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::THROTTLE_POSITION ) ).signalValue.doubleVal,
                      (double)0x80 * 100 / 255 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedOxygenSensorX_1 )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x14, 0x10, 0x20 };
    EmissionInfo info;

    for ( PID pid = toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ); pid <= toUType( EmissionPIDs::OXYGEN_SENSOR8_1 ); ++pid )
    {
        txPDUData[1] = pid;
        ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
        ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( pid | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) ).signalValue.doubleVal,
                          (double)0x10 / 200 );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( pid | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) ).signalValue.doubleVal,
                          (double)0x20 * 100 / 128 - 100 );
    }
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedRuntimeSinceEngineStart )
{
    // RuntimeSinceEngineStart of 500 seconds
    std::vector<uint8_t> txPDUData = { 0x41, 0x1F, 0x01, 0xF4 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x1F }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::RUNTIME_SINCE_ENGINE_START ) ).signalValue.doubleVal, 500 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedDistanceTraveledWithMIL )
{
    // DistanceTraveledWithMIL of 10 km
    std::vector<uint8_t> txPDUData = { 0x41, 0x21, 0x00, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x21 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::DISTANCE_TRAVELED_WITH_MIL ) ).signalValue.doubleVal, 10 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedOxygenSensorX_2 )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x24, 0x10, 0x20, 0x30, 0x40 };
    EmissionInfo info;

    for ( PID pid = toUType( EmissionPIDs::OXYGEN_SENSOR1_2 ); pid <= toUType( EmissionPIDs::OXYGEN_SENSOR8_2 ); ++pid )
    {
        txPDUData[1] = pid;
        ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
        ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( pid | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) ).signalValue.doubleVal,
                          ( 256 * 0x10 + 0x20 ) * 0.0000305 );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( pid | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) ).signalValue.doubleVal,
                          ( 256 * 0x30 + 0x40 ) * 0.000122 );
    }
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedFuelTankLevel )
{
    // FuelTankLevel of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x2F, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x2F }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::FUEL_TANK_LEVEL ) ).signalValue.doubleVal, 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedDistanceTraveledSinceClearedDTC )
{
    // DistanceTraveledSinceClearedDTC of 10 km
    std::vector<uint8_t> txPDUData = { 0x41, 0x31, 0x00, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x31 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::DISTANCE_TRAVELED_SINCE_CLEARED_DTC ) ).signalValue.doubleVal,
        10 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedControlModuleVoltage )
{
    // ControlModuleVoltage of 25V
    std::vector<uint8_t> txPDUData = { 0x41, 0x42, 0x64, 0x64 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x42 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::CONTROL_MODULE_VOLTAGE ) ).signalValue.doubleVal,
                      ( 256.0 * 100 + 100 ) / 1000 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedRelativeThrottlePosition )
{
    // RelativeThrottlePosition of 50 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x45, 0x80 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x45 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::RELATIVE_THROTTLE_POSITION ) ).signalValue.doubleVal,
        (double)0x80 * 100 / 255 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedAmbientAireTemperature )
{
    // AmbientAireTemperature of 30 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x46, 0x46 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x46 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::AMBIENT_AIR_TEMPERATURE ) ).signalValue.doubleVal,
                      30 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedRelativePedalPosition )
{
    // RelativePedalPosition of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x5A, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x5A }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::RELATIVE_ACCELERATOR_PEDAL_POSITION ) ).signalValue.doubleVal,
        100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedBatteryRemainingLife )
{
    // BatteryRemainingLife of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x5B, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x5B }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::HYBRID_BATTERY_PACK_REMAINING_LIFE ) ).signalValue.doubleVal,
        100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineOilTemperature )
{
    // EngineOilTemperature of  30 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x5C, 0x46 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x5C }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_OIL_TEMPERATURE ) ).signalValue.doubleVal,
                      30 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedDriverDemandTorque )
{
    // DriverDemandTorque of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x61, 0xE1 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x61 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::DRIVER_DEMAND_PERCENT_TORQUE ) ).signalValue.doubleVal, 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedActualEngineTorque )
{
    // ActualEngineTorque of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x62, 0xE1 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x62 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ACTUAL_PERCENT_TORQUE ) ).signalValue.doubleVal,
                      100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedReferenceEngineTorque )
{
    // ReferenceEngineTorque of 25700 Nm
    std::vector<uint8_t> txPDUData = { 0x41, 0x63, 0x64, 0x64 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x63 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_REFERENCE_PERCENT_TORQUE ) ).signalValue.doubleVal,
        25700 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedBoostPressureControl )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x70, 0x3F, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x0F };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x70 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x3F );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x00 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedVariableGeometryTurboControl )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x71, 0x3F, 0x10, 0x20, 0x30, 0x40, 0x0F };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x71 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x3F );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x10 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x20 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x30 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x40 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x00 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedBoostPressureControlAndVariableGeometryTurboControl )
{
    std::vector<uint8_t> txPDUData = { 0x41,
                                       0x70,
                                       0x3F,
                                       0x64,
                                       0x64,
                                       0x64,
                                       0x64,
                                       0x64,
                                       0x64,
                                       0x64,
                                       0x64,
                                       0x0F,
                                       0x71,
                                       0x3F,
                                       0x10,
                                       0x20,
                                       0x30,
                                       0x40,
                                       0x0F };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x70, 0x71 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x3F );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        803.125 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::BOOST_PRESSURE_CONTROL ) | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x00 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x3F );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x10 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x20 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x30 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        (double)100 / 255 * 0x40 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x03 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::VARIABLE_GEOMETRY_TURBO_CONTROL ) | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x00 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineRunTime )
{
    std::vector<uint8_t> txPDUData = {
        0x41, 0x7F, 0x08, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x7F }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_RUN_TIME ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x08 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_RUN_TIME ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        16909060 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_RUN_TIME ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        16909060 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_RUN_TIME ) | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        16909060 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderExhaustGasTemperatureSensor )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x98, 0xFF, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x98 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::EXHAUST_GAS_TEMPERATURE_SENSORA ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0xFF );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::EXHAUST_GAS_TEMPERATURE_SENSORA ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x1020 * 0.1 - 40 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::EXHAUST_GAS_TEMPERATURE_SENSORA ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x3040 * 0.1 - 40 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::EXHAUST_GAS_TEMPERATURE_SENSORA ) | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x5060 * 0.1 - 40 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues
            .at( toUType( EmissionPIDs::EXHAUST_GAS_TEMPERATURE_SENSORA ) | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x7080 * 0.1 - 40 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedTransmissionActualGear )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0xA4, 0xFF, 0xF0, 0xAA, 0x55 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0xA4 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::TRANSMISSION_ACTUAL_GEAR ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0xFF );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::TRANSMISSION_ACTUAL_GEAR ) | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0x0F );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::TRANSMISSION_ACTUAL_GEAR ) | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
            .signalValue.doubleVal,
        0xAA55 * 0.001 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedOdometer )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0xA6, 0x01, 0x10, 0xAA, 0x55 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0xA6 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ODOMETER ) | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT ) )
                          .signalValue.doubleVal,
                      0x0110AA55 * 0.1 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedFuelPressure )
{
    // Engine load of 450 pka
    std::vector<uint8_t> txPDUData = { 0x41, 0x0A, 0x96 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x0A }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::FUEL_PRESSURE ) ).signalValue.doubleVal, 450 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineSpeed )
{
    // Engine load of 666 rpm
    std::vector<uint8_t> txPDUData = { 0x41, 0x0C, 0x0A, 0x6B };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x0C }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_SPEED ) ).signalValue.doubleVal,
                      ( 256.0 * 0x0A + 0x6B ) / 4 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedVehicleSpeed )
{
    // Engine load of 35 kph
    std::vector<uint8_t> txPDUData = { 0x41, 0x0D, 0x23 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x0D }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::VEHICLE_SPEED ) ).signalValue.doubleVal, 35 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDs )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x04, 0x99, 0x05, 0x6E, 0x0A, 0x96, 0x0C, 0x0A, 0x6B, 0x0D, 0x23 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x04, 0x05, 0x0A, 0x0C, 0x0D }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_LOAD ) ).signalValue.doubleVal, 60 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ) ).signalValue.doubleVal, 70 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::FUEL_PRESSURE ) ).signalValue.doubleVal, 450 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_SPEED ) ).signalValue.doubleVal,
                      ( 256.0 * 0x0A + 0x6B ) / 4 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::VEHICLE_SPEED ) ).signalValue.doubleVal, 35 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDsWhereResponseOrderDifferentThanRequestOrder )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x04, 0x99, 0x05, 0x6E, 0x0A, 0x96, 0x0C, 0x0A, 0x6B, 0x0D, 0x23 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x0D, 0x04, 0x0A, 0x0C, 0x05 }, txPDUData, info ) );
    ASSERT_EQ( info.mSID, SID::CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_LOAD ) ).signalValue.doubleVal, 60 );
    ASSERT_DOUBLE_EQ(
        info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ) ).signalValue.doubleVal, 70 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::FUEL_PRESSURE ) ).signalValue.doubleVal, 450 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::ENGINE_SPEED ) ).signalValue.doubleVal,
                      ( 256.0 * 0x0A + 0x6B ) / 4 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues.at( toUType( EmissionPIDs::VEHICLE_SPEED ) ).signalValue.doubleVal, 35 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDsWrongPDU )
{

    std::vector<uint8_t> txPDUData = { 0x41, 0xAA, 0x99, 0xBB, 0xBB, 0xCC, 0xEE, 0xCC, 0xFF };
    EmissionInfo info;

    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0xAA, 0xCC, 0xFF }, txPDUData, info ) );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDsNegativeResponse )
{

    std::vector<uint8_t> txPDUData = { 0x78, 0x09, 0x99 };
    EmissionInfo info;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 0x09 }, txPDUData, info ) );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderExtractDTCStringTest )
{

    std::vector<uint8_t> txPDUData = { 0x01, 0x96 };
    std::string dtc;

    ASSERT_TRUE( decoder.extractDTCString( txPDUData[0], txPDUData[1], dtc ) );
    ASSERT_EQ( dtc, "P0196" );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderdecodeDTCsTest )
{
    // 4 DTC reported on each domain
    std::vector<uint8_t> txPDUData = { 0x43, 0x04, 0x01, 0x43, 0x41, 0x96, 0x81, 0x48, 0xC1, 0x48 };
    DTCInfo info;

    ASSERT_TRUE( decoder.decodeDTCs( SID::STORED_DTC, txPDUData, info ) );
    ASSERT_EQ( info.mDTCCodes.size(), 4 );
    ASSERT_EQ( info.mDTCCodes[0], "P0143" );
    ASSERT_EQ( info.mDTCCodes[1], "C0196" );
    ASSERT_EQ( info.mDTCCodes[2], "B0148" );
    ASSERT_EQ( info.mDTCCodes[3], "U0148" );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderdecodeDTCsCorruptDataTest )
{
    // Wrong count of DTCs
    std::vector<uint8_t> txPDUData = { 0x43, 0x04, 0x01, 0x43 };
    DTCInfo info;

    ASSERT_FALSE( decoder.decodeDTCs( SID::STORED_DTC, txPDUData, info ) );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderdecodeVIN )
{
    // Wrong count of DTCs
    std::vector<uint8_t> txPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                                       0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    std::string vin;

    ASSERT_TRUE( decoder.decodeVIN( txPDUData, vin ) );
    ASSERT_EQ( vin, "1G1JC5444R7252367" );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderNoVIN )
{
    // Wrong count of DTCs
    std::vector<uint8_t> txPDUData = { 0x49, 0x02 };
    std::string vin;

    ASSERT_FALSE( decoder.decodeVIN( txPDUData, vin ) );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderCorruptFormulaTest )
{
    PID pid = 0x66;
    const auto &originalFormula = decoderDictPtr->at( pid ).mSignals[0];
    // corrupt mFirstBitPosition to out of bound
    decoderDictPtr->at( pid ).mSignals[0].mFirstBitPosition = 80;
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x99 };
    EmissionInfo info;

    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
    decoderDictPtr->at( pid ).mSignals[0] = originalFormula;
    // corrupt mSizeInBits to out of bound
    decoderDictPtr->at( pid ).mSignals[0].mSizeInBits = 80;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );

    decoderDictPtr->at( pid ).mSignals[0] = originalFormula;
    // corrupt mSizeInBits to be invalid
    decoderDictPtr->at( pid ).mSignals[0].mSizeInBits = 33;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );

    decoderDictPtr->at( pid ).mSignals[0] = originalFormula;
    // corrupt mFirstBitPosition to be invalid. Because mSizeInBit is 8, First Bit position
    // has to be aligned with byte
    decoderDictPtr->at( pid ).mSignals[0].mFirstBitPosition = 2;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
}

// In this test, we aim to test OBDDataDecoder with ECU response mismatch with decoder manifest.
// We shall expect decodeEmissionPIDs not decode this invalid ECU response.
TEST_F( OBDDataDecoderTest, OBDDataDecoderWithECUResponseMismatchWithDecoderManifest )
{
    // Below ECU response contains PID 107 which has mismatched response than decoder manifest
    std::vector<uint8_t> txPDUData = { 0x41, 107, 0, 108, 0, 109, 0, 13, 102, 12, 252, 110, 0, 111, 0, 112, 0 };
    EmissionInfo info;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { 107, 108, 109, 110, 111, 112 }, txPDUData, info ) );
    ASSERT_EQ( info.mPIDsToValues.size(), 0 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineLoadCorruptDecoderDictionaryTest )
{
    PID pid = 0x04;
    // corrupt decoder dictionary pointer to nullptr
    decoderDictPtr = nullptr;
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x99 };
    EmissionInfo info;

    ASSERT_FALSE( decoder.decodeEmissionPIDs( SID::CURRENT_STATS, { pid }, txPDUData, info ) );
}
