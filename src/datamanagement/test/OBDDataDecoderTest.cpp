
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

#include "OBDDataDecoder.h"
#include <gtest/gtest.h>
#include <unistd.h>

using namespace Aws::IoTFleetWise::DataManagement;

// For testing purpose, Signal ID is defined as PID | (signal_order << PID_SIGNAL_BITS_LEFT_SHIFT)
#define PID_SIGNAL_BITS_LEFT_SHIFT 8

class OBDDataDecoderTest : public ::testing::Test
{
protected:
    OBDDataDecoder decoder;
    std::shared_ptr<OBDDecoderDictionary> decoderDictPtr;
    void
    SetUp() override
    {
        decoderDictPtr = std::make_shared<OBDDecoderDictionary>();
        // In final product, signal ID comes from Cloud, edge doesn't generate signal ID
        // Below signal ID initialization got implemented only for Edge testing.
        // In this test, signal ID are defined as PID | (signal order) << 8
        for ( PID pid = FUEL_SYSTEM_STATUS; pid <= ODOMETER; ++pid )
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
        decoder.setDecoderDictionary( decoderDictPtr );
    }

    void
    TearDown() override
    {
    }
};

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedSupportedPIDs )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x00, 0xBF, 0xBF, 0xA8, 0x91, 0x20, 0x80, 0x00, 0x00, 0x00 };
    SupportedPIDs pids;

    ASSERT_TRUE( decoder.decodeSupportedPIDs( CURRENT_STATS, txPDUData, pids ) );
    // Expect only what we support in the SW
    // The PID list should NOT include the Supported PID ID that was requested i.e. 0X00
    ASSERT_EQ( pids.size(), 18 );
    ASSERT_EQ( std::find( pids.begin(), pids.end(), 0X00 ), pids.end() );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineLoad )
{
    // Engine load of 60 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x04, 0x99 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );

    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_LOAD], 60 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineTemperature )
{
    // Engine load of 70 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x05, 0x6E };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_COOLANT_TEMPERATURE], 70 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedFuelTrim )
{
    // Fuel Trim on all banks of value 50
    // Raw Value is 192 -100 * 1.28
    std::vector<uint8_t> txPDUData = { 0x41, 0x06, 0xC0, 0x07, 0xC0, 0x08, 0xC0, 0x09, 0xC0 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[SHORT_TERM_FUEL_TRIM_BANK_1], 50 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[SHORT_TERM_FUEL_TRIM_BANK_2], 50 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[LONG_TERM_FUEL_TRIM_BANK_1], 50 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[LONG_TERM_FUEL_TRIM_BANK_2], 50 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedIntakeManifoldPressure )
{
    // IntakeManifoldPressure of 200 pka
    std::vector<uint8_t> txPDUData = { 0x41, 0x0B, 0xC8 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[INTAKE_MANIFOLD_ABSOLUTE_PRESSURE], 200 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedIntakeAirFLowTemperature )
{
    // IntakeAirFLowTemperature of 30 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x0F, 0x46 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[INTAKE_AIR_FLOW_TEMPERATURE], 30 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMAFRate )
{
    // MAFRate od 25 grams/sec
    std::vector<uint8_t> txPDUData = { 0x41, 0x10, 0x0A, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[MAF_RATE], ( 256.0 * 0x0A + 0x0A ) / 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedThrottlePosition )
{
    // ThrottlePosition of 50 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x11, 0x80 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[THROTTLE_POSITION], (double)0x80 * 100 / 255 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedOxygenSensorX_1 )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x14, 0x10, 0x20 };
    EmissionInfo info;

    for ( PID pid = OXYGEN_SENSOR1_1; pid <= OXYGEN_SENSOR8_1; ++pid )
    {
        txPDUData[1] = pid;
        ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
        ASSERT_EQ( info.mSID, CURRENT_STATS );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues[pid | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], (double)0x10 / 200 );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues[pid | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                          (double)0x20 * 100 / 128 - 100 );
    }
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedRuntimeSinceEngineStart )
{
    // RuntimeSinceEngineStart of 500 seconds
    std::vector<uint8_t> txPDUData = { 0x41, 0x1F, 0x01, 0xF4 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[RUNTIME_SINCE_ENGINE_START], 500 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedDistanceTraveledWithMIL )
{
    // DistanceTraveledWithMIL of 10 km
    std::vector<uint8_t> txPDUData = { 0x41, 0x21, 0x00, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[DISTANCE_TRAVELED_WITH_MIL], 10 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedOxygenSensorX_2 )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x24, 0x10, 0x20, 0x30, 0x40 };
    EmissionInfo info;

    for ( PID pid = OXYGEN_SENSOR1_2; pid <= OXYGEN_SENSOR8_2; ++pid )
    {
        txPDUData[1] = pid;
        ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
        ASSERT_EQ( info.mSID, CURRENT_STATS );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues[pid | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                          ( 256 * 0x10 + 0x20 ) * 0.0000305 );
        ASSERT_DOUBLE_EQ( info.mPIDsToValues[pid | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                          ( 256 * 0x30 + 0x40 ) * 0.000122 );
    }
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedFuelTankLevel )
{
    // FuelTankLevel of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x2F, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[FUEL_TANK_LEVEL], 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedDistanceTraveledSinceClearedDTC )
{
    // DistanceTraveledSinceClearedDTC of 10 km
    std::vector<uint8_t> txPDUData = { 0x41, 0x31, 0x00, 0x0A };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[DISTANCE_TRAVELED_SINCE_CLEARED_DTC], 10 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedControlModuleVoltage )
{
    // ControlModuleVoltage of 25V
    std::vector<uint8_t> txPDUData = { 0x41, 0x42, 0x64, 0x64 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[CONTROL_MODULE_VOLTAGE], ( 256.0 * 100 + 100 ) / 1000 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedRelativeThrottlePosition )
{
    // RelativeThrottlePosition of 50 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x45, 0x80 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[RELATIVE_THROTTLE_POSITION], (double)0x80 * 100 / 255 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedAmbientAireTemperature )
{
    // AmbientAireTemperature of 30 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x46, 0x46 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[AMBIENT_AIR_TEMPERATURE], 30 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedRelativePedalPosition )
{
    // RelativePedalPosition of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x5A, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[RELATIVE_ACCELERATOR_PEDAL_POSITION], 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedBatteryRemainingLife )
{
    // BatteryRemainingLife of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x5B, 0xFF };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[HYBRID_BATTERY_PACK_REMAINING_LIFE], 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineOilTemperature )
{
    // EngineOilTemperature of  30 deg
    std::vector<uint8_t> txPDUData = { 0x41, 0x5C, 0x46 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_OIL_TEMPERATURE], 30 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedDriverDemandTorque )
{
    // DriverDemandTorque of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x61, 0xE1 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[DRIVER_DEMAND_PERCENT_TORQUE], 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedActualEngineTorque )
{
    // ActualEngineTorque of 100 %
    std::vector<uint8_t> txPDUData = { 0x41, 0x62, 0xE1 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ACTUAL_PERCENT_TORQUE], 100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedReferenceEngineTorque )
{
    // ReferenceEngineTorque of 25700 Nm
    std::vector<uint8_t> txPDUData = { 0x41, 0x63, 0x64, 0x64 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_REFERENCE_PERCENT_TORQUE], 25700 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedBoostPressureControl )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x70, 0x3F, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x0F };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x3F );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x00 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedVariableGeometryTurboControl )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x71, 0x3F, 0x10, 0x20, 0x30, 0x40, 0x0F };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x3F );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x10 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x20 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x30 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x40 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x00 );
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

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x3F );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT )], 803.125 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[BOOST_PRESSURE_CONTROL | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x00 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x3F );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x10 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x20 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x30 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      (double)100 / 255 * 0x40 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 5 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 6 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x03 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VARIABLE_GEOMETRY_TURBO_CONTROL | ( 7 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x00 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineRunTime )
{
    std::vector<uint8_t> txPDUData = {
        0x41, 0x7F, 0x08, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_RUN_TIME | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x08 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_RUN_TIME | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )], 16909060 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_RUN_TIME | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )], 16909060 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_RUN_TIME | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT )], 16909060 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderExhaustGasTemperatureSensor )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x98, 0xFF, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[EXHAUST_GAS_TEMPERATURE_SENSORA | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0xFF );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[EXHAUST_GAS_TEMPERATURE_SENSORA | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      0x1020 * 0.1 - 40 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[EXHAUST_GAS_TEMPERATURE_SENSORA | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      0x3040 * 0.1 - 40 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[EXHAUST_GAS_TEMPERATURE_SENSORA | ( 3 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      0x5060 * 0.1 - 40 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[EXHAUST_GAS_TEMPERATURE_SENSORA | ( 4 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      0x7080 * 0.1 - 40 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedTransmissionActualGear )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0xA4, 0xFF, 0xF0, 0xAA, 0x55 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[TRANSMISSION_ACTUAL_GEAR | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0xFF );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[TRANSMISSION_ACTUAL_GEAR | ( 1 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x0F );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[TRANSMISSION_ACTUAL_GEAR | ( 2 << PID_SIGNAL_BITS_LEFT_SHIFT )],
                      0xAA55 * 0.001 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedOdometer )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0xA6, 0x01, 0x10, 0xAA, 0x55 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ODOMETER | ( 0 << PID_SIGNAL_BITS_LEFT_SHIFT )], 0x0110AA55 * 0.1 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedFuelPressure )
{
    // Engine load of 450 pka
    std::vector<uint8_t> txPDUData = { 0x41, 0x0A, 0x96 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[FUEL_PRESSURE], 450 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineSpeed )
{
    // Engine load of 666 rpm
    std::vector<uint8_t> txPDUData = { 0x41, 0x0C, 0x0A, 0x6B };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_SPEED], ( 256.0 * 0x0A + 0x6B ) / 4 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedVehicleSpeed )
{
    // Engine load of 35 kph
    std::vector<uint8_t> txPDUData = { 0x41, 0x0D, 0x23 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VEHICLE_SPEED], 35 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDs )
{
    std::vector<uint8_t> txPDUData = { 0x41, 0x04, 0x99, 0x05, 0x6E, 0x0A, 0x96, 0x0C, 0x0A, 0x6B, 0x0D, 0x23 };
    EmissionInfo info;

    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    ASSERT_EQ( info.mSID, CURRENT_STATS );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_LOAD], 60 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_COOLANT_TEMPERATURE], 70 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[FUEL_PRESSURE], 450 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[ENGINE_SPEED], ( 256.0 * 0x0A + 0x6B ) / 4 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[VEHICLE_SPEED], 35 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDsWrongPDU )
{

    std::vector<uint8_t> txPDUData = { 0x41, 0xAA, 0x99, 0xBB, 0xBB, 0xCC, 0xEE, 0xCC, 0xFF };
    EmissionInfo info;

    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedMultiplePIDsNegativeResponse )
{

    std::vector<uint8_t> txPDUData = { 0x78, 0x09, 0x99 };
    EmissionInfo info;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
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

    ASSERT_TRUE( decoder.decodeDTCs( STORED_DTC, txPDUData, info ) );
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

    ASSERT_FALSE( decoder.decodeDTCs( STORED_DTC, txPDUData, info ) );
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
    decoder.setDecoderDictionary( decoderDictPtr );
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x99 };
    EmissionInfo info;

    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    decoderDictPtr->at( pid ).mSignals[0] = originalFormula;
    // corrupt mSizeInBits to out of bound
    decoderDictPtr->at( pid ).mSignals[0].mSizeInBits = 80;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );

    decoderDictPtr->at( pid ).mSignals[0] = originalFormula;
    // corrupt mSizeInBits to be invalid
    decoderDictPtr->at( pid ).mSignals[0].mSizeInBits = 33;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );

    decoderDictPtr->at( pid ).mSignals[0] = originalFormula;
    // corrupt mFirstBitPosition to be invalid. Because mSizeInBit is 8, First Bit position
    // has to be aligned with byte
    decoderDictPtr->at( pid ).mSignals[0].mFirstBitPosition = 2;
    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
}

// In this test, we aim to test OBDDataDecoder to handle scenario when some PID decoding rule is missing in decoder
// dictionary. The decoder shall continue navigating the payload based on local decoder table (mode1PIDs)
TEST_F( OBDDataDecoderTest, OBDDataDecoderWithIncompleteDecoderingRule )
{
    // erase PID 36 from decoder dictionary
    decoderDictPtr->erase( 36 );
    decoder.setDecoderDictionary( decoderDictPtr );
    // The payload contains two PID: 36 and 45. PID 36 is no longer in decoder dictionary.
    // 200 is invalid PID and decoder shall not attempt to decode it
    std::vector<uint8_t> txPDUData = { 0x41, 36, 128, 28, 73, 12, 45, 0, 200, 100 };
    EmissionInfo info;
    ASSERT_TRUE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
    // As PID 36 is missing in decoder dictionary, the entire PID 36 shall be skipped. Only PID 45 will be decoded and
    // stored into map
    ASSERT_EQ( info.mPIDsToValues.size(), 1 );
    ASSERT_DOUBLE_EQ( info.mPIDsToValues[45], -100 );
}

TEST_F( OBDDataDecoderTest, OBDDataDecoderDecodedEngineLoadCorruptDecoderDictionaryTest )
{
    PID pid = 0x04;
    // corrupt decoder dictionary pointer to nullptr
    decoder.setDecoderDictionary( nullptr );
    std::vector<uint8_t> txPDUData = { 0x41, pid, 0x99 };
    EmissionInfo info;

    ASSERT_FALSE( decoder.decodeEmissionPIDs( CURRENT_STATS, txPDUData, info ) );
}