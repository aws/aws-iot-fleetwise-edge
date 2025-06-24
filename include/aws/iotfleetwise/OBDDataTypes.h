// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/EnumUtility.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
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
    SID mSID{ SID::INVALID_SERVICE_MODE };
    std::map<uint32_t, DecodedSignalValue> mPIDsToValues;
};

// Structure of a single PID OBD request.
struct OBDRequest
{
    SID mSID{ SID::INVALID_SERVICE_MODE };
    PID mPID{ 0 };
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
