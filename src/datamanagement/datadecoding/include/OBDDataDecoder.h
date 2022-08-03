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

#pragma once

// Includes
#include "ClockHandler.h"
#include "IDecoderDictionary.h"
#include "LoggingModule.h"
#include "OBDDataTypes.h"
#include "Timer.h"
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Linux;

/**
 * @brief decoder dictionary to be used to decode OBD PID message to signals.
 */
using OBDDecoderDictionary = std::unordered_map<PID, CANMessageFormat>;

/**
 * @brief define shared pointer type for OBD-II PID decoder dictionary
 */
using ConstOBDDecoderDictionaryConstPtr = const std::shared_ptr<const OBDDecoderDictionary>;

/**
 * @brief OBD Data Decoder. Decodes OBD ECU responses according
 * to the J1979 specifications
 */

class OBDDataDecoder
{

public:
    OBDDataDecoder();
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
    bool decodeSupportedPIDs( const SID &sid, const std::vector<uint8_t> &inputData, SupportedPIDs &supportedPIDs );

    /**
     * @brief Decodes an ECU response to a list of Emission related PIDs.
     * Validates first from the first byte whether it's a positive response.
     * @param sid SID for which the PIDs where requested.
     * @param inputData raw response from the ECU
     * @param info Output vector of PID physical values
     * @return True if we received a positive response and decoded at least one supported PID.
     * needed.
     */
    bool decodeEmissionPIDs( const SID &sid, const std::vector<uint8_t> &inputData, EmissionInfo &info );

    /**
     * @brief Decodes DTCs from the ECU response,
     * Validates first from the first byte whether it's a positive response.
     * @param sid SID for which the PIDs where requested. This is either Mode 3 or Mode 7
     * @param inputData raw response from the ECU
     * @param info Output vector of DTCs
     * @return True if we received a positive response and decoded the DTCs.
     * A positive response also can mean that no DTCs were reported by the ECU.
     */
    static bool decodeDTCs( const SID &sid, const std::vector<uint8_t> &inputData, DTCInfo &info );

    /**
     * @brief Decodes VIN from the ECU response,
     * Validates first from the first byte whether it's a positive response.
     * @param inputData raw response from the ECU
     * @param vin output string
     * @return True if we received a positive response and decoded the VIN.
     */
    static bool decodeVIN( const std::vector<uint8_t> &inputData, std::string &vin );

    static bool extractDTCString( const uint8_t &firstByte, const uint8_t &secondByte, std::string &dtcString );

    /**
     * @brief Update OBD Data Decoder module with decoder dictionary
     *
     * @param dictionary Const shared pointer to Const OBD Decoder Dictionary
     * @return None
     */
    void setDecoderDictionary( ConstOBDDecoderDictionaryConstPtr &dictionary );

private:
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    // shared pointer to decoder dictionary
    std::shared_ptr<const OBDDecoderDictionary> mDecoderDictionaryConstPtr;
    /**
     * @brief Validate signal formula
     * @param pid
     * @param formula
     * @return True if formula is valid.
     */
    bool isFormulaValid( PID pid, CANSignalFormat formula );
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
