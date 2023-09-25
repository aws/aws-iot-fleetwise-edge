// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "MessageTypes.h"
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include "Timer.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief decoder dictionary to be used to decode OBD PID message to signals.
 */
using OBDDecoderDictionary = std::unordered_map<PID, CANMessageFormat>;

/**
 * @brief OBD Data Decoder. Decodes OBD ECU responses according
 * to the J1979 specifications
 */

class OBDDataDecoder
{

public:
    OBDDataDecoder( std::shared_ptr<OBDDecoderDictionary> &decoderDictionary );
    ~OBDDataDecoder() = default;

    OBDDataDecoder( const OBDDataDecoder & ) = delete;
    OBDDataDecoder &operator=( const OBDDataDecoder & ) = delete;
    OBDDataDecoder( OBDDataDecoder && ) = delete;
    OBDDataDecoder &operator=( OBDDataDecoder && ) = delete;

    /**
     * @brief Extracts from the ECU response the Supported PIDs for the given SID.
     * Validates first from the first byte whether it's a positive response.
     * @param sid SID for which the PIDs where requested.
     * @param inputData raw response from the ECU
     * @param supportedPIDs Output vector of PIDs
     * @return True if we received a positive response and extracted the PIDs.
     * needed.
     */
    static bool decodeSupportedPIDs( const SID sid,
                                     const std::vector<uint8_t> &inputData,
                                     SupportedPIDs &supportedPIDs );

    /**
     * @brief Decodes an ECU response to a list of Emission related PIDs.
     * Validates first from the first byte whether it's a positive response.
     * @param sid SID for which the PIDs where requested.
     * @param pids List of PIDs that FWE requested from ECU
     * @param inputData raw response from the ECU
     * @param info Output vector of PID physical values
     * @return True if we received a positive response and decoded at least one supported PID.
     * needed.
     */
    bool decodeEmissionPIDs( const SID sid,
                             const std::vector<PID> &pids,
                             const std::vector<uint8_t> &inputData,
                             EmissionInfo &info );

    /**
     * @brief Decodes DTCs from the ECU response,
     * Validates first from the first byte whether it's a positive response.
     * @param sid SID for which the PIDs where requested. This is either Mode 3 or Mode 7
     * @param inputData raw response from the ECU
     * @param info Output vector of DTCs
     * @return True if we received a positive response and decoded the DTCs.
     * A positive response also can mean that no DTCs were reported by the ECU.
     */
    static bool decodeDTCs( const SID sid, const std::vector<uint8_t> &inputData, DTCInfo &info );

    /**
     * @brief Decodes VIN from the ECU response,
     * Validates first from the first byte whether it's a positive response.
     * @param inputData raw response from the ECU
     * @param vin output string
     * @return True if we received a positive response and decoded the VIN.
     */
    static bool decodeVIN( const std::vector<uint8_t> &inputData, std::string &vin );

    static bool extractDTCString( const uint8_t &firstByte, const uint8_t &secondByte, std::string &dtcString );

private:
    Timer mTimer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    // shared pointer to decoder dictionary
    std::shared_ptr<OBDDecoderDictionary> &mDecoderDictionary;

    /**
     * @brief Uses the given formula to transform the raw data and add the results to info
     * @param pid The PID this formula is related to
     * @param formula The formula that will be used to calculate the final value
     * @param inputData Raw response from the ECU
     * @param byteCounter The index of inputData where this value starts
     * @param info Output vector of PID physical values
     */
    void calculateValueFromFormula( PID pid,
                                    const CANSignalFormat &formula,
                                    const std::vector<uint8_t> &inputData,
                                    size_t byteCounter,
                                    EmissionInfo &info );

    /**
     * @brief Validate signal formula
     * @param pid
     * @param formula
     * @return True if formula is valid.
     */
    bool isFormulaValid( PID pid, const CANSignalFormat &formula );

    /**
     * @brief Check if PIDs response length is valid. When the response consists of multiple PIDs,
     * this function will check whether each PID exists in response and whether each PID's response
     * matches with decoder dictionary
     * @param pids List of PIDs that FWE requested from ECU
     * @param ecuResponse The PID response from ECU
     * @return true if response length is valid
     * @return false if response length is invalid
     */
    bool isPIDResponseValid( const std::vector<PID> &pids, const std::vector<uint8_t> &ecuResponse );
};

} // namespace IoTFleetWise
} // namespace Aws
