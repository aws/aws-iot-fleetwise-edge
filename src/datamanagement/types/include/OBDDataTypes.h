// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "EnumUtility.h"
#include "TimeTypes.h"
#include "datatypes/VehicleDataSourceTypes.h"
#include <array>
#include <map>
#include <memory>
#include <unordered_set>
#include <vector>
// Default Keep Alive Interval.
#define OBD_KEEP_ALIVE_SECONDS 2

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::Platform::Utility;

using SignalValue = double;

// List of OBD Service IDs/ Modes
enum class SIDs : uint32_t
{
    INVALID_SERVICE_MODE = 0x00,      // invalid service mode
    CURRENT_STATS = 0x01,             // Current Stats
    STATS_SINCE_FREEZE_FRAME = 0x02,  // Stats since Freeze Frame
    STORED_DTC = 0x03,                // Request Stored DTCs
    CLEAR_DTC = 0x04,                 // Clear DTCs and MIL
    OXGEN_SENSOR_MODE_NON_CAN = 0x05, // Request Oxygen Sensor Monitoring, Not Supported over CAN
    OXGEN_SENSOR_MODE = 0x06,         // Request Oxygen Sensor Monitoring
    PENDING_DTC = 0x07,               // Request Pending DTCs
    TESTING = 0x08,                   // Testing related SID
    VEHICLE_INFO = 0x09               // Request Vehicle Information
};

using SID = SIDs;
using PID = uint8_t;
using SupportedPIDs = std::vector<PID>;
constexpr PID INVALID_PID = UINT8_MAX;

/**
 * @brief Formula class for PID Signals. This formula is designed to decode any signal from the PID.
 * Example 1. Signal Mass Air Flow Sensor A come from PID 0x66, byte B and C
 *           scaling : 0.03125
 *           offset  : 0
 *           byteOffset: 1
 *           numOfBytes: 2
 *           bitShift: 0
 *           bitMaskLen: 8
 * Example 2. Boost Pressure B Control Status come from PID 0x70, byte J, bit 2, 3
 *           scaling : 1.0
 *           offset  : 0
 *           byteOffset: 9
 *           numOfBytes: 1
 *           bitShift: 2
 *           bitMaskLen: 2
 */
class PIDSignalFormula
{
public:
    uint32_t signalID{ 0 };
    double scaling{ 1.0 };
    double offset{ 0.0 };
    size_t byteOffset{ 0 };  // the start byte for this signal
    size_t numOfBytes{ 1 };  // number of bytes for this signal
    uint8_t bitShift{ 0 };   // If signal is bits, the number of right shift to be performed
    uint8_t bitMaskLen{ 8 }; // If signal is bits, the length of mask after shifting
    // Note below constructor is only intended for current validation when data pipeline is missing.
    // Once data pipeline got connected, the formula will come from decoder manifest
    /**
     * @brief default constructor when no input was provided
     */
    PIDSignalFormula() = default;

    /**
     * @brief constructor when provided the byte offset
     *
     * @param byteOffsetIn start byte for the signal
     */
    PIDSignalFormula( size_t byteOffsetIn )
        : byteOffset( byteOffsetIn )
    {
    }

    /**
     * @brief constructor when provided the byte offset, scaling, offset and number of bytes
     *        This constructor is intended for signal containing multiple bytes
     *
     * @param byteOffsetIn start byte for the signal
     * @param scalingIn scaling to be applied to the raw data
     * @param offsetIn offset to be applied to the raw data
     * @param numOfBytesIn number of bytes for the signal
     */
    PIDSignalFormula( size_t byteOffsetIn, double scalingIn, double offsetIn, size_t numOfBytesIn )
        : scaling( scalingIn )
        , offset( offsetIn )
        , byteOffset( byteOffsetIn )
        , numOfBytes( numOfBytesIn )
    {
    }

    /**
     * @brief constructor when provided the byte offset and bitmask
     *        This constructor is intended for bitmask signal
     *
     * @param byteOffsetIn start byte for the signal
     * @param bitShiftIn Right shift to be performed on this byte
     * @param bitMaskIn The bitmask to be applied after shifting
     */
    PIDSignalFormula( size_t byteOffsetIn, uint8_t bitShiftIn, uint8_t bitMaskIn )
        : byteOffset( byteOffsetIn )
        , bitShift( bitShiftIn )
        , bitMaskLen( bitMaskIn )
    {
    }
};

// Struct represent PID information: id, return length and formula for each signal
struct PIDInfo
{
    PID pid;       // id for PID, used to query ECU
    size_t retLen; // expected number of bytes in response
    std::vector<PIDSignalFormula>
        formulas; // formula per signal. For multi-signal PID, this would contains multiple formulas.
};

// clang-format off
// Subset of Emission related PIDs that are supported by this SW
// Every new PID we support should be updated on those next
// structs
enum class EmissionPIDs
{
    PIDS_SUPPORTED_01_20                                                           = 0X00,
    FUEL_SYSTEM_STATUS                                                             = 0X03,
    ENGINE_LOAD                                                                    = 0X04,
    ENGINE_COOLANT_TEMPERATURE                                                     = 0X05,
    SHORT_TERM_FUEL_TRIM_BANK_1                                                    = 0X06,
    LONG_TERM_FUEL_TRIM_BANK_1                                                     = 0X07,
    SHORT_TERM_FUEL_TRIM_BANK_2                                                    = 0X08,
    LONG_TERM_FUEL_TRIM_BANK_2                                                     = 0X09,
    FUEL_PRESSURE                                                                  = 0X0A,
    INTAKE_MANIFOLD_ABSOLUTE_PRESSURE                                              = 0X0B,
    ENGINE_SPEED                                                                   = 0X0C,
    VEHICLE_SPEED                                                                  = 0X0D,
    TIMING_ADVANCE                                                                 = 0X0E,
    INTAKE_AIR_FLOW_TEMPERATURE                                                    = 0X0F,
    MAF_RATE                                                                       = 0X10,
    THROTTLE_POSITION                                                              = 0X11,
    OXYGEN_SENSORS_PRESENT                                                         = 0X13,
    OXYGEN_SENSOR1_1                                                               = 0X14,
    OXYGEN_SENSOR2_1                                                               = 0X15,
    OXYGEN_SENSOR3_1                                                               = 0X16,
    OXYGEN_SENSOR4_1                                                               = 0X17,
    OXYGEN_SENSOR5_1                                                               = 0X18,
    OXYGEN_SENSOR6_1                                                               = 0X19,
    OXYGEN_SENSOR7_1                                                               = 0X1A,
    OXYGEN_SENSOR8_1                                                               = 0X1B,
    RUNTIME_SINCE_ENGINE_START                                                     = 0X1F,
    PIDS_SUPPORTED_21_40                                                           = 0X20,
    DISTANCE_TRAVELED_WITH_MIL                                                     = 0X21,
    FUEL_RAIL_PRESSURE                                                             = 0X22,
    FUEL_RAIL_GAUGE_PRESSURE                                                       = 0X23,
    OXYGEN_SENSOR1_2                                                               = 0X24,
    OXYGEN_SENSOR2_2                                                               = 0X25,
    OXYGEN_SENSOR3_2                                                               = 0X26,
    OXYGEN_SENSOR4_2                                                               = 0X27,
    OXYGEN_SENSOR5_2                                                               = 0X28,
    OXYGEN_SENSOR6_2                                                               = 0X29,
    OXYGEN_SENSOR7_2                                                               = 0X2A,
    OXYGEN_SENSOR8_2                                                               = 0X2B,
    EGR_ERROR                                                                      = 0X2D,
    FUEL_TANK_LEVEL                                                                = 0X2F,
    WARM_UPS_SINCE_CODES_CLEARED                                                   = 0X30,
    DISTANCE_TRAVELED_SINCE_CLEARED_DTC                                            = 0X31,
    EVAP_SYSTEM_VAPOR_PRESSURE                                                     = 0X32,
    ABSOLUTE_BAROMETRIC_PRESSURE                                                   = 0X33,
    OXYGEN_SENSOR1_3                                                               = 0X34,
    OXYGEN_SENSOR2_3                                                               = 0X35,
    OXYGEN_SENSOR3_3                                                               = 0X36,
    OXYGEN_SENSOR4_3                                                               = 0X37,
    OXYGEN_SENSOR5_3                                                               = 0X38,
    OXYGEN_SENSOR6_3                                                               = 0X39,
    OXYGEN_SENSOR7_3                                                               = 0X3A,
    OXYGEN_SENSOR8_3                                                               = 0X3B,
    CATALYST_TEMPERATURE_BANK1_SENSOR1                                             = 0X3C,
    CATALYST_TEMPERATURE_BANK2_SENSOR1                                             = 0X3D,
    CATALYST_TEMPERATURE_BANK1_SENSOR2                                             = 0X3E,
    CATALYST_TEMPERATURE_BANK2_SENSOR2                                             = 0X3F,
    PIDS_SUPPORTED_41_60                                                           = 0X40,
    CONTROL_MODULE_VOLTAGE                                                         = 0X42,
    ABSOLUTE_LOAD_VALUE                                                            = 0X43,
    COMMANDED_AIR_FUEL_EQUIVALENCE_RATIO                                           = 0X44,
    RELATIVE_THROTTLE_POSITION                                                     = 0X45,
    AMBIENT_AIR_TEMPERATURE                                                        = 0X46,
    ABSOLUTE_THROTTLE_POSITION_B                                                   = 0X47,
    ABSOLUTE_THROTTLE_POSITION_C                                                   = 0X48,
    ACCELERATOR_PEDAL_POSITION_D                                                   = 0X49,
    ACCELERATOR_PEDAL_POSITION_E                                                   = 0X4A,
    ACCELERATOR_PEDAL_POSITION_F                                                   = 0X4B,
    TIME_RUN_WITH_MIL_ON                                                           = 0X4D,
    TIME_SINCE_TROUBLE_CODES_CLEARED                                               = 0X4E,
    FUEL_TYPE                                                                      = 0X51,
    ALCOHOL_FUEL_PERCENTAGE                                                        = 0X52,
    FUEL_RAIL_ABSOLUTE_PRESSURE                                                    = 0X59,
    RELATIVE_ACCELERATOR_PEDAL_POSITION                                            = 0X5A,
    HYBRID_BATTERY_PACK_REMAINING_LIFE                                             = 0X5B,
    ENGINE_OIL_TEMPERATURE                                                         = 0X5C,
    FUEL_INJECTION_TIMING                                                          = 0X5D,
    ENGINE_FUEL_RATE                                                               = 0X5E,
    PIDS_SUPPORTED_61_80                                                           = 0X60,
    DRIVER_DEMAND_PERCENT_TORQUE                                                   = 0X61,
    ACTUAL_PERCENT_TORQUE                                                          = 0X62,
    ENGINE_REFERENCE_PERCENT_TORQUE                                                = 0X63,
    ENGINE_PERCENT_TORQUE_DATA                                                     = 0X64,
    MASS_AIR_FLOW_SENSOR                                                           = 0X66,
    ENGINE_COOLANT_TEMPERATURE_1_2                                                 = 0X67,
    INTAKE_AIR_TEMPERATURE_SENSOR                                                  = 0X68,
    COMMANDED_EGR_AND_EGR_ERROR                                                    = 0X69,
    COMMANDED_DIESEL_INTAKE_AIR_FLOW_CONTROL_AND_RELATIVE_INTAKE_AIR_FLOW_POSITION = 0X6A,
    EXHAUST_GAS_RECIRCULATION_TEMPERATURE                                          = 0X6B,
    COMMANDED_THROTTLE_ACTUATOR_CONTROL_AND_RELATIVE_THROTTLE_POSITION             = 0X6C,
    FUEL_PRESSURE_CONTROL_SYSTEM                                                   = 0X6D,
    INJECTION_PRESSURE_CONTROL_SYSTEM                                              = 0X6E,
    TURBOCHARGER_COMPRESSOR_INLET_PRESSURE                                         = 0X6F,
    BOOST_PRESSURE_CONTROL                                                         = 0X70,
    VARIABLE_GEOMETRY_TURBO_CONTROL                                                = 0X71,
    WASTEGATE_CONTROL                                                              = 0X72,
    EXHAUST_PRESSURE                                                               = 0X73,
    TURBOCHARGER_RPM                                                               = 0X74,
    TURBOCHARGER_TEMPERATURE_A                                                     = 0X75,
    TURBOCHARGER_TEMPERATURE_B                                                     = 0X76,
    CHARGE_AIR_COOLER_TEMPERATURE                                                  = 0X77,
    EXHAUST_GAS_TEMPERATURE_BANK1                                                  = 0X78,
    EXHAUST_GAS_TEMPERATURE_BANK2                                                  = 0X79,
    DIESEL_PARTICULATE_FILTER1                                                     = 0X7A,
    DIESEL_PARTICULATE_FILTER2                                                     = 0X7B,
    DIESEL_PARTICULATE_FILTER_TEMPERATURE                                          = 0X7C,
    ENGINE_RUN_TIME                                                                = 0X7F,
    PIDS_SUPPORTED_81_A0                                                           = 0X80,
    NOX_SENSOR                                                                     = 0X83,
    MANIFOLD_SURFACE_TEMPERATURE                                                   = 0X84,
    NOX_REAGENT_SYSTEM                                                             = 0X85,
    PARTICULATE_MATTER_SENSOR                                                      = 0X86,
    INTAKE_MANIFOLD_ABSOLUTE_PRESSURE_A_B                                          = 0X87,
    O2_SENSOR_WIDE_RANGE                                                           = 0X8C,
    THROTTLE_POSITION_G                                                            = 0X8D,
    ENGINE_FRICTION_PERCENT_TORQUE                                                 = 0X8E,
    FUEL_SYSTEM_CONTROL                                                            = 0X92,
    EXHAUST_GAS_TEMPERATURE_SENSORA                                                = 0X98,
    EXHAUST_GAS_TEMPERATURE_SENSORB                                                = 0X99,
    HYBRID_EV_VEHICLE_SYSTEM_DATA_BATTERY_VOLTAGE                                  = 0X9A,
    DIESEL_EXHAUST_FLUID_SENSOR_DATA                                               = 0X9B,
    O2_SENSOR_DATA                                                                 = 0X9C,
    FUEL_RATE                                                                      = 0X9D,
    ENGINE_EXHAUST_FLOW_RATE                                                       = 0X9E,
    FUEL_SYSTEM_PERCENTAGE_USE                                                     = 0X9F,
    PIDS_SUPPORTED_A1_C0                                                           = 0XA0,
    CYLINDER_FUEL_RATE                                                             = 0XA2,
    TRANSMISSION_ACTUAL_GEAR                                                       = 0XA4,
    ODOMETER                                                                       = 0XA6,
    PIDS_SUPPORTED_C1_E0                                                           = 0XC0,
    HVESS_RECOMMENDED_MAX_SOC                                                      = 0xC1
};
// clang-format on

static constexpr uint8_t SUPPORTED_PID_STEP = 0x20;

static constexpr std::array<PID, 8> supportedPIDRange = { { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0 } };

// Mode 2
static constexpr std::array<PID, 1> mode2PIDs = { {
    0x02 // DTC that caused freeze frame to be stored.
} };

// Mode 9
static constexpr std::array<PID, 12> mode9PIDs = { {
    0x00, // Service 9 supported PIDs (01 to 20)
    0x01, // VIN Message Count in PID 02. Only for ISO 9141-2, ISO 14230-4 and SAE J1850.
    0x02, // Vehicle Identification Number (VIN)
    0x03, // Calibration ID message count for PID 04. Only for ISO 9141-2, ISO 14230-4 and SAE J1850.
    0x04, // Calibration ID
    0x05, // Calibration verification numbers (CVN) message count for PID 06. Only for ISO 9141-2, ISO
          // 14230-4 and SAE J1850.
    0x06, // Calibration Verification Numbers (CVN) Several CVN can be output (4 bytes each) the number of
          // CVN and CALID must match
    0x07, // In-use performance tracking message count for PID 08 and 0B. Only for ISO 9141-2, ISO 14230-4
          // and SAE J1850
    0x08, // In-use performance tracking for spark ignition vehicles
    0x09, // ECU name message count for PID 0A
    0x0A, // ECU name
    0x0B, // In-use performance tracking for compression ignition vehicles
} };

inline constexpr PID
getPID( SID sid, size_t index )
{
    PID returnPID = INVALID_PID;
    switch ( sid )
    {
    case SID::CURRENT_STATS:
        if ( index < static_cast<size_t>( supportedPIDRange.back() + SUPPORTED_PID_STEP ) )
        {
            returnPID = static_cast<PID>( index );
        }
        break;
    case SID::STATS_SINCE_FREEZE_FRAME:
        if ( index < mode2PIDs.size() )
        {
            returnPID = mode2PIDs[index];
        }
        break;
    case SID::OXGEN_SENSOR_MODE_NON_CAN:
        // This SID is not supported over CAN. Skipping
        break;
    case SID::VEHICLE_INFO:
        if ( index < mode9PIDs.size() )
        {
            returnPID = mode9PIDs[index];
        }
        break;

    default:
        break;
    }

    return returnPID;
}

// List of Parsed DTC codes detected on the bus
// e.g. P1462
struct DTCInfo
{
    SID mSID{ SID::INVALID_SERVICE_MODE };
    Timestamp receiveTime{ 0 };
    std::vector<std::string> mDTCCodes;
    bool
    hasItems() const
    {
        return !mDTCCodes.empty();
    }
};

// List of Emission related PIDs requested
// on the bus and there physical values
// e.g. PID = 0x0C( RPM)
struct EmissionInfo
{
    SID mSID;
    std::map<uint32_t, SignalValue> mPIDsToValues;
};

// Structure of a single PID OBD request.
struct OBDRequest
{
    SID mSID;
    PID mPID;
};
// DTC related types
enum class DTCDomains
{
    POWERTRAIN,
    CHASSIS,
    BODY,
    NETWORK
};

// VIN Request
static constexpr OBDRequest vehicleIdentificationNumberRequest = { static_cast<SID>( 0x09 ), static_cast<PID>( 0x02 ) };

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
