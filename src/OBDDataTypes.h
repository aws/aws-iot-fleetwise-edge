// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "EnumUtility.h"
#include "TimeTypes.h"
#include "VehicleDataSourceTypes.h"
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

union OBDValue {
    double doubleVal;
    uint64_t uint64Val;
    int64_t int64Val;
};

struct OBDSignal
{
    OBDValue signalValue;
    SignalType signalType;

    template <typename T>
    OBDSignal( T val, SignalType type )
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

// List of OBD Service IDs/ Modes
enum class SID : uint32_t
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
    VEHICLE_INFO = 0x09,              // Request Vehicle Information
    MAX = 0x0A
};

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
// on the bus and their physical values
// e.g. PID = 0x0C( RPM)
struct EmissionInfo
{
    SID mSID;
    std::map<uint32_t, OBDSignal> mPIDsToValues;
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

} // namespace IoTFleetWise
} // namespace Aws
