// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// This files creates a big initialization function to create mode1PIDs,
// so do not include this file in the main source code, only in unit tests.

#include "OBDDataTypes.h"

namespace Aws
{
namespace IoTFleetWise
{

// Struct represent PID information: id, return length and formula for each signal
struct PIDInfo
{
    PID pid;       // id for PID, used to query ECU
    size_t retLen; // expected number of bytes in response
    std::vector<PIDSignalFormula>
        formulas; // formula per signal. For multi-signal PID, this would contains multiple formulas.
};

// This table is only used by unit test.
// FWE will use decoder manifest received from the cloud
const std::array<struct PIDInfo, 172> mode1PIDs = { {
    { 0x00, 4, { { 0, 1.0, 0, 4 } } }, // PIDs supported [01 - 20]
    { 0x01, 4, { {} } },               // Monitor status since DTCs cleared.(MIL) status and number of DTCs.
    { 0x02, 2, { {} } },               // Freeze DTC
    { 0x03, 2, { { 0, 1.0, 0, 1 }, { 1, 1.0, 0, 1 } } },                    // Fuel system status
    { 0x04, 1, { { 0, (double)100 / 255, 0, 1 } } },                        // Calculated engine load
    { 0x05, 1, { { 0, 1.0, -40.0, 1 } } },                                  // Engine coolant temperature
    { 0x06, 1, { { 0, (double)100 / 128, -100.0, 1 } } },                   // Short term fuel trim-bank 1
    { 0x07, 1, { { 0, (double)100 / 128, -100.0, 1 } } },                   // Long term fuel trim-bank 1
    { 0x08, 1, { { 0, (double)100 / 128, -100.0, 1 } } },                   // Short term fuel trim-bank 2
    { 0x09, 1, { { 0, (double)100 / 128, -100.0, 1 } } },                   // Long term fuel trim-bank 2
    { 0x0A, 1, { { 0, 3.0, 0, 1 } } },                                      // Fuel pressure (gauge pressure)
    { 0x0B, 1, { { 0, 1.0, 0, 1 } } },                                      // Intake manifold absolute pressure
    { 0x0C, 2, { { 0, 0.25, 0, 2 } } },                                     // Engine speed
    { 0x0D, 1, { { 0, 1.0, 0, 1 } } },                                      // Vehicle speed
    { 0x0E, 1, { { 0, 0.5, -64.0, 1 } } },                                  // Timing advance
    { 0x0F, 1, { { 0, 1.0, -40.0, 1 } } },                                  // Intake air temperature
    { 0x10, 2, { { 0, 0.01, 0, 2 } } },                                     // Mass air flow sensor (MAF) air flow rate
    { 0x11, 1, { { 0, (double)100 / 255, 0, 1 } } },                        // Throttle position
    { 0x12, 1, { {} } },                                                    // Commanded secondary air status
    { 0x13, 1, { { 0, 1.0, 0, 1 } } },                                      // Oxygen sensors present (in 2 banks)
    { 0x14, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 1
    { 0x15, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 2
    { 0x16, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 3
    { 0x17, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 4
    { 0x18, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 5
    { 0x19, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 6
    { 0x1A, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 7
    { 0x1B, 2, { { 0, 0.005, 0, 1 }, { 1, (double)100 / 128, -100, 1 } } }, // Oxygen Sensor 8
    { 0x1C, 1, { {} } },                                                    // OBD standards the vehicle conforms to
    { 0x1D, 1, { {} } },                                                    // Oxygen sensors present (in 4 banks)
    { 0x1E, 1, { {} } },                                                    // Auxiliary input status
    { 0x1F, 2, { { 0, 1.0, 0, 2 } } },                                      // Run time since engine start
    { 0x20, 4, { { 0, 1.0, 0, 4 } } },                                      // PIDs supported [21 - 40]
    { 0x21, 2, { { 0, 1.0, 0, 2 } } },   // Distance traveled with malfunction indicator lamp (MIL) on
    { 0x22, 2, { { 0, 0.079, 0, 2 } } }, // Fuel Rail Pressure (relative to manifold vacuum)
    { 0x23, 2, { { 0, 10.0, 0, 2 } } },  // Fuel Rail Gauge Pressure (diesel, or gasoline direct injection)
    { 0x24, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 1
    { 0x25, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 2
    { 0x26, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 3
    { 0x27, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 4
    { 0x28, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 5
    { 0x29, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 6
    { 0x2A, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 7
    { 0x2B, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.000122, 0, 2 } } },        // Oxygen Sensor 8
    { 0x2C, 1, { {} } },                                                   // Commanded EGR
    { 0x2D, 1, { { 0, (double)100 / 128, -100, 1 } } },                    // EGR Error
    { 0x2E, 1, { {} } },                                                   // Commanded evaporative purge
    { 0x2F, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Fuel Tank Level Input
    { 0x30, 1, { { 0, 1.0, 0, 1 } } },                                     // Warm-ups since codes cleared
    { 0x31, 2, { { 0, 1.0, 0, 2 } } },                                     // Distance traveled since codes cleared
    { 0x32, 2, { { 0, 0.25, 0, 2 } } },                                    // Evap. System Vapor Pressure
    { 0x33, 1, { { 0, 1.0, 0, 1 } } },                                     // Absolute Barometric Pressure
    { 0x34, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 1
    { 0x35, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 2
    { 0x36, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 3
    { 0x37, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 4
    { 0x38, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 5
    { 0x39, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 6
    { 0x3A, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 7
    { 0x3B, 4, { { 0, 0.0000305, 0, 2 }, { 2, 0.00390625, -128.0, 2 } } }, // Oxygen Sensor 8
    { 0x3C, 2, { { 0, 0.1, -40.0, 2 } } },                                 // Catalyst Temperature: Bank 1, Sensor 1
    { 0x3D, 2, { { 0, 0.1, -40.0, 2 } } },                                 // Catalyst Temperature: Bank 2, Sensor 1
    { 0x3E, 2, { { 0, 0.1, -40.0, 2 } } },                                 // Catalyst Temperature: Bank 1, Sensor 2
    { 0x3F, 2, { { 0, 0.1, -40.0, 2 } } },                                 // Catalyst Temperature: Bank 2, Sensor 2
    { 0x40, 4, { { 0, 1.0, 0, 4 } } },                                     // PIDs supported [41 - 60]
    { 0x41, 4, { {} } },                                                   // Monitor status this drive cycle
    { 0x42, 2, { { 0, 0.001, 0, 2 } } },                                   // Control module voltage
    { 0x43, 2, { { 0, (double)100 / 255, 0, 2 } } },                       // Absolute load value
    { 0x44, 2, { { 0, 0.0000305, 0, 2 } } },                               // Commanded Air-Fuel Equivalence Ratio
    { 0x45, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Relative throttle position
    { 0x46, 1, { { 0, 1.0, -40.0, 1 } } },                                 // Ambient air temperature
    { 0x47, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Absolute throttle position B
    { 0x48, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Absolute throttle position C
    { 0x49, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Accelerator pedal position D
    { 0x4A, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Accelerator pedal position E
    { 0x4B, 1, { { 0, (double)100 / 255, 0, 1 } } },                       // Accelerator pedal position F
    { 0x4C, 1, { {} } },                                                   // Commanded throttle actuator
    { 0x4D, 2, { { 0, 1.0, 0, 1 } } },                                     // Time run with MIL on
    { 0x4E, 2, { { 0, 1.0, 0, 1 } } },                                     // Time since trouble codes cleared
    { 0x4F,
      4,
      { { 0, 1.0, 0, 1 }, { 1, 1.0, 0, 1 }, { 2, 1.0, 0, 1 }, { 3, 10.0, 0, 1 } } }, // Maximum value for Fuel-Air
                                                                                     // equivalence ratio,
    // oxygen sensor voltage, oxygen sensor current, and intake manifold absolute pressure
    { 0x50, 4, { {} } },                                // Maximum value for air flow rate from mass air flow sensor
    { 0x51, 1, { { 0, 1.0, 0, 1 } } },                  // Fuel Type
    { 0x52, 1, { { 0, (double)100 / 255, 0, 1 } } },    // Ethanol fuel %
    { 0x53, 2, { {} } },                                // Absolute Evap system Vapor Pressure
    { 0x54, 2, { {} } },                                // Evap system vapor pressure
    { 0x55, 2, { {} } },                                // Short term secondary oxygen sensor trim, A: bank 1, B: bank 3
    { 0x56, 2, { {} } },                                // Long term secondary oxygen sensor trim, A: bank 1, B: bank 3
    { 0x57, 2, { {} } },                                // Short term secondary oxygen sensor trim, A: bank 2, B: bank 4
    { 0x58, 2, { {} } },                                // Long term secondary oxygen sensor trim, A: bank 2, B: bank 4
    { 0x59, 2, { { 0, 10.0, 0, 2 } } },                 // Fuel rail absolute pressure
    { 0x5A, 1, { { 0, (double)100 / 255, 0, 1 } } },    // Relative accelerator pedal position
    { 0x5B, 1, { { 0, (double)100 / 255, 0, 1 } } },    // Hybrid battery pack remaining life
    { 0x5C, 1, { { 0, 1.0, -40.0, 1 } } },              // Engine oil temperature
    { 0x5D, 2, { { 0, (double)1 / 128, -210.0, 2 } } }, // Fuel injection timing
    { 0x5E, 2, { { 0, 0.05, 0, 2 } } },                 // Engine fuel rate
    { 0x5F, 1, { {} } },                                // Emission requirements to which vehicle is designed
    { 0x60, 4, { { 0, 1.0, 0, 4 } } },                  // PIDs supported [61 - 80]
    { 0x61, 1, { { 0, 1.0, -125.0, 1 } } },             // Driver's demand engine - percent torque
    { 0x62, 1, { { 0, 1.0, -125.0, 1 } } },             // Actual engine - percent torque
    { 0x63, 2, { { 0, 1.0, 0, 2 } } },                  // Engine reference torque
    { 0x64,
      5,
      { { 0, 1.0, -125.0, 1 },
        { 1, 1.0, -125.0, 1 },
        { 2, 1.0, -125.0, 1 },
        { 3, 1.0, -125.0, 1 },
        { 4, 1.0, -125.0, 1 } } },                                                 // Engine percent torque data
    { 0x65, 2, { {} } },                                                           // Auxiliary input / output supported
    { 0x66, 5, { { 0, 1.0, 0, 1 }, { 1, 0.03125, 0, 2 }, { 3, 0.03125, 0, 2 } } }, // Mass air flow sensor
    { 0x67, 3, { { 0, 1.0, 0, 1 }, { 1, 1.0, -40.0, 1 }, { 2, 1.0, -40.0, 1 } } }, // Engine coolant temperature
    { 0x68,
      7,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, -40.0, 1 },
        { 2, 1.0, -40.0, 1 },
        { 3, 1.0, -40.0, 1 },
        { 4, 1.0, -40.0, 1 },
        { 5, 1.0, -40.0, 1 },
        { 6, 1.0, -40.0, 1 } } }, // Intake air temperature sensor
    { 0x69,
      7,
      { { 0, 1.0, 0, 1 },
        { 1, (double)100 / 255, 0, 1 },
        { 2, (double)100 / 255, 0, 1 },
        { 3, (double)100 / 255, 0, 1 },
        { 4, (double)100 / 255, 0, 1 },
        { 5, (double)100 / 255, 0, 1 },
        { 6, (double)100 / 255, 0, 1 } } }, // Commanded EGR and EGR Error
    { 0x6A,
      5,
      { { 0, 1.0, 0, 1 },
        { 1, (double)100 / 255, 0, 1 },
        { 2, (double)100 / 255, 0, 1 },
        { 3, (double)100 / 255, 0, 1 },
        { 4, (double)100 / 255, 0, 1 } } }, // Commanded Diesel intake air flow control and relative intake air flow
                                            // position

    { 0x6B,
      5,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, -40, 1 },
        { 2, 1.0, -40, 1 },
        { 3, 1.0, -40, 1 },
        { 4, 1.0, -40, 1 } } }, // Exhaust gas recirculation temperature
    { 0x6C,
      5,
      { { 0, 1.0, 0, 1 },
        { 1, (double)100 / 255, 0, 1 },
        { 2, (double)100 / 255, 0, 1 },
        { 3, (double)100 / 255, 0, 1 },
        { 4, (double)100 / 255, 0, 1 } } }, // Commanded throttle actuator control and relative throttle position
    { 0x6D,
      11,
      { { 0, 1.0, 0, 1 },
        { 1, 10.0, 0, 2 },
        { 3, 10.0, 0, 2 },
        { 5, 1.0, -40, 1 },
        { 6, 10.0, 0, 2 },
        { 8, 10.0, 0, 2 },
        { 10, 1.0, -40, 1 } } }, // Fuel pressure control system
    { 0x6E,
      9,
      { { 0, 1.0, 0, 1 },
        { 1, 10.0, 0, 2 },
        { 3, 10.0, 0, 2 },
        { 5, 10.0, 0, 2 },
        { 7, 10.0, 0, 2 } } },                                             // Injection pressure control system
    { 0x6F, 3, { { 0, 1.0, 0, 1 }, { 1, 1.0, 0, 1 }, { 2, 1.0, 0, 1 } } }, // Turbocharger compressor inlet pressure
    { 0x70,
      10,
      { { 0, 1.0, 0, 1 },
        { 1, 0.03125, 0, 2 },
        { 3, 0.03125, 0, 2 },
        { 5, 0.03125, 0, 2 },
        { 7, 0.03125, 0, 2 },
        { 9, 0, 2 },
        { 9, 2, 2 },
        { 9, 4, 4 } } }, // Boost pressure control
    { 0x71,
      6,
      { { 0, 1.0, 0, 1 },
        { 1, (double)100 / 255, 0, 1 },
        { 2, (double)100 / 255, 0, 1 },
        { 3, (double)100 / 255, 0, 1 },
        { 4, (double)100 / 255, 0, 1 },
        { 5, 0, 2 },
        { 5, 2, 2 },
        { 5, 4, 4 } } }, // Variable Geometry turbo (VGT) control
    { 0x72,
      5,
      { { 0, 1.0, 0, 1 },
        { 1, (double)100 / 255, 0, 1 },
        { 2, (double)100 / 255, 0, 1 },
        { 3, (double)100 / 255, 0, 1 },
        { 4, (double)100 / 255, 0, 1 } } },                                  // Wastegate control
    { 0x73, 5, { { 0, 1.0, 0, 1 }, { 1, 0.01, 0, 2 }, { 3, 0.01, 0, 2 } } }, // Exhaust pressure
    { 0x74, 5, { { 0, 1.0, 0, 1 }, { 1, 10.0, 0, 2 }, { 3, 10.0, 0, 2 } } }, // Turbocharger RPM
    { 0x75,
      7,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, -40, 1 },
        { 2, 1.0, -40, 1 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 } } }, // Turbocharger temperature
    { 0x76,
      7,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, -40, 1 },
        { 2, 1.0, -40, 1 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 } } }, // Turbocharger temperature
    { 0x77,
      5,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, -40, 1 },
        { 2, 1.0, -40, 1 },
        { 3, 1.0, -40, 1 },
        { 4, 1.0, -40, 1 } } }, // Charge air cooler temperature (CACT)
    { 0x78,
      9,
      { { 0, 1.0, 0, 1 },
        { 1, 0.1, -40, 2 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 },
        { 7, 0.1, -40, 2 } } }, // Exhaust Gas temperature (EGT) Bank 1
    { 0x79,
      9,
      { { 0, 1.0, 0, 1 },
        { 1, 0.1, -40, 2 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 },
        { 7, 0.1, -40, 2 } } }, // Exhaust Gas temperature (EGT) Bank 2
    { 0x7A, 7, { { 0, 1.0, 0, 1 }, { 1, 0.01, 0, 2 }, { 3, 0.01, 0, 2 }, { 5, 0.01, 0, 2 } } }, // Diesel particulate
                                                                                                // filter (DPF)
    { 0x7B, 7, { { 0, 1.0, 0, 1 }, { 1, 0.01, 0, 2 }, { 3, 0.01, 0, 2 }, { 5, 0.01, 0, 2 } } }, // Diesel particulate
                                                                                                // filter (DPF)
    { 0x7C,
      9,
      { { 0, 1.0, 0, 1 },
        { 1, 0.1, -40, 2 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 },
        { 7, 0.1, -40, 2 } } },        // Diesel Particulate filter (DPF) temperature
    { 0x7D, 1, { { 0, 1.0, 0, 1 } } }, // NOx NTE (Not-To-Exceed) control area status
    { 0x7E, 1, { { 0, 1.0, 0, 1 } } }, // PM NTE (Not-To-Exceed) control area status
    { 0x7F, 13, { { 0, 1.0, 0, 1 }, { 1, 1.0, 0, 4 }, { 5, 1.0, 0, 4 }, { 9, 1.0, 0, 4 } } }, // Engine run time
    { 0x80, 4, { { 0, 1.0, 0, 4 } } }, // PIDs supported [81 - A0]
    { 0x81, 21, { {} } },              // Engine run time for Auxiliary Emissions Control Device(AECD)
    { 0x82, 21, { {} } },              // Engine run time for Auxiliary Emissions Control Device(AECD)
    { 0x83,
      9,
      { { 0, 1.0, 0, 1 }, { 1, 0.1, 0, 2 }, { 3, 0.1, 0, 2 }, { 5, 0.1, 0, 2 }, { 7, 0.1, 0, 2 } } }, // NOx sensor
    { 0x84, 1, { { 0, 1.0, -40, 1 } } }, // Manifold surface temperature
    { 0x85,
      10,
      { { 0, 1.0, 0, 1 },
        { 1, 0.005, 0, 2 },
        { 3, 0.005, 0, 2 },
        { 5, (double)100 / 255, 0, 1 },
        { 6, 1.0, 0, 4 } } },                                                      // NOx reagent system
    { 0x86, 5, { { 0, 1.0, 0, 1 }, { 1, 0.0125, 0, 2 }, { 3, 0.0125, 0, 2 } } },   // Particulate matter (PM) sensor
    { 0x87, 5, { { 0, 1.0, 0, 1 }, { 1, 0.03125, 0, 2 }, { 3, 0.03125, 0, 2 } } }, // Intake manifold absolute pressure
    { 0x88, 13, { {} } },                                                          // SCR Induce System
    { 0x89, 41, { {} } },                                                          // Run Time for AECD #11-#15
    { 0x8A, 41, { {} } },                                                          // Run Time for AECD #16-#20
    { 0x8B, 7, { {} } },                                                           // Diesel Aftertreatment
    { 0x8C,
      17,
      { { 0, 1.0, 0, 1 },
        { 1, 0.001526, 0, 2 },
        { 3, 0.001526, 0, 2 },
        { 5, 0.001526, 0, 2 },
        { 7, 0.001526, 0, 2 },
        { 9, 0.000122, 0, 2 },
        { 11, 0.000122, 0, 2 },
        { 13, 0.000122, 0, 2 },
        { 15, 0.000122, 0, 2 } } },                      // O2 Sensor (Wide Range)
    { 0x8D, 1, { { 0, (double)100 / 255, 0, 1 } } },     // Throttle Position G
    { 0x8E, 1, { { 0, 1.0, -125.0, 1 } } },              // Engine Friction - Percent Torque
    { 0x8F, 5, { {} } },                                 // PM Sensor Bank 1 & 2
    { 0x90, 3, { {} } },                                 // WWH-OBD Vehicle OBD System Information
    { 0x91, 5, { {} } },                                 // WWH-OBD Vehicle OBD System Information
    { 0x92, 2, { { 0, 1.0, 0, 1 }, { 1, 1.0, 0, 1 } } }, // Fuel System Control
    { 0x93, 3, { { 0, 1.0, 0, 1 }, { 1, 1.0, 0, 2 } } }, // WWH-OBD Vehicle OBD Counters support
    { 0x94,
      12,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, 0, 1 },
        { 2, 1.0, 0, 2 },
        { 4, 1.0, 0, 2 },
        { 6, 1.0, 0, 2 },
        { 8, 1.0, 0, 2 },
        { 10, 1.0, 0, 2 } } }, // NOx Warning And Inducement System
    { 0x95, 0, { {} } },       // dummy
    { 0x96, 0, { {} } },       // dummy
    { 0x97, 0, { {} } },       // dummy
    { 0x98,
      9,
      { { 0, 1.0, 0, 1 },
        { 1, 0.1, -40, 2 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 },
        { 7, 0.1, -40, 2 } } }, // Exhaust Gas Temperature Sensor
    { 0x99,
      9,
      { { 0, 1.0, 0, 1 },
        { 1, 0.1, -40, 2 },
        { 3, 0.1, -40, 2 },
        { 5, 0.1, -40, 2 },
        { 7, 0.1, -40, 2 } } }, // Exhaust Gas Temperature Sensor
    { 0x9A,
      6,
      { { 0, 1.0, 0, 1 },
        { 1, 1.0, 0, 1 },
        { 2, 0.015625, 0, 2 },
        { 4, 0.1, -3276.8, 2 } } }, // Hybrid/EV Vehicle System Data, Battery, Voltage
    { 0x9B,
      4,
      { { 0, 1.0, 0, 1 },
        { 1, 0.25, 0, 1 },
        { 0, 1.0, -40, 1 },
        { 1, (double)100 / 255, 0, 1 } } }, // Diesel Exhaust Fluid Sensor Data
    { 0x9C,
      17,
      { { 0, 1.0, 0, 1 },
        { 1, 0.001526, 0, 2 },
        { 3, 0.001526, 0, 2 },
        { 5, 0.001526, 0, 2 },
        { 7, 0.001526, 0, 2 },
        { 9, 0.000122, 0, 2 },
        { 11, 0.000122, 0, 2 },
        { 13, 0.000122, 0, 2 },
        { 15, 0.000122, 0, 2 } } },                        // O2 Sensor Data
    { 0x9D, 4, { { 0, 0.02, 0, 2 }, { 2, 0.02, 0, 2 } } }, // Engine Fuel Rate
    { 0x9E, 2, { { 0, 0.2, 0, 2 } } },                     // Engine Exhaust Flow Rate
    { 0x9F,
      9,
      {
          { 0, 1.0, 0, 1 },
          { 1, (double)100 / 255, 0, 1 },
          { 2, (double)100 / 255, 0, 1 },
          { 3, (double)100 / 255, 0, 1 },
          { 4, (double)100 / 255, 0, 1 },
          { 5, (double)100 / 255, 0, 1 },
          { 6, (double)100 / 255, 0, 1 },
          { 7, (double)100 / 255, 0, 1 },
          { 8, (double)100 / 255, 0, 1 },
      } },                                                              // Fuel System Percentage Use
    { 0xA0, 4, { {} } },                                                // PIDs supported [A1 - C0]
    { 0xA1, 9, { {} } },                                                // NOx Sensor Corrected Data
    { 0xA2, 2, { { 0, 0.03125, 0, 2 } } },                              // Cylinder Fuel Rate
    { 0xA3, 9, { {} } },                                                // Evap System Vapor Pressure
    { 0xA4, 4, { { 0, 1.0, 0, 1 }, { 1, 4, 4 }, { 2, 0.001, 0, 2 } } }, // Transmission Actual Gear
    { 0xA5, 4, { {} } },                                                // Diesel Exhaust Fluid Dosing
    { 0xA6, 4, { { 0, 0.1, 0, 4 } } },                                  // Odometer
    { 0xC0, 4, { { 0, 1.0, 0, 4 } } },                                  // PIDs supported [C1 - E0]
    { 0xC1, 1, { {} } }                                                 // HVESS Recommended Maximum State Of Charge
} };

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

} // namespace IoTFleetWise
} // namespace Aws
