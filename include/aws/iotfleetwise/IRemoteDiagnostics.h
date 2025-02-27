// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/*! UDSStatusMask DTC status mask for more detail please refer ISO 14229-1*/
enum class UDSStatusMask
{
    TEST_FAILED = 0x01,
    TEST_FAILED_THIS_OP_CYCLE = 0x02,
    PENDING_DTC = 0x04,
    CONFIRMED_DTC = 0x08,
    TEST_NOT_COMPLETED_SINCE_LAST_CLEAR = 0x10,
    TEST_FAILED_SINCE_LAST_CLEAR = 0x20,
    TEST_NOT_COMPLETED_THIS_OP_CYCLE = 0x40,
    WARNING_INDICATOR_REQUESTED = 0x80
};

/*! UDSSubFunction UDS DTC subfunction; for more detail please refer ISO 14229-1*/
// coverity[autosar_cpp14_a7_2_4_violation] Need to start from 1
// coverity[misra_cpp_2008_rule_8_5_3_violation] Need to start from 1
enum class UDSSubFunction
{
    NO_DTC_BY_STATUS_MASK = 0x01,
    DTC_BY_STATUS_MASK,
    DTC_SNAPSHOT_IDENTIFICATION,
    DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER,
    DTC_STORED_DATA_BY_RECORD_NUMBER,
    DTC_EXT_DATA_RECORD_BY_DTC_NUMBER,
    NO_DTC_BY_SEVERITY_MASK_RECORD,
    DTC_BY_SEVERITY_MASK_RECORD,
    SEVERITY_INFORMATION_OF_DTC,
    SUPPORTED_DTCS,
    FIRST_TEST_FAILED_DTC,
    FIRST_CONFIRMED_DTC,
    MOST_RECENT_TEST_FAILED_DTC,
    MOST_RECENT_CONFIRMED_DTC,
    MIRROR_MR_DTC_BY_STATUS_MASK,
    MIRROR_MR_DTC_EXT_DATA_RECORD_BY_DTC,
    NO_MIRROR_MR_DTC_BY_STATUS_MASK,
    NO_EMISSION_OBD_DTC_BY_STATUS_MASK,
    EMISSION_OBD_DTC_BY_STATUS_MASK,
    DTC_FAULT_DETECTION_COUNTER,
    DTC_WITH_PERMANENT_STATUS,
    DTC_EXT_DATA_RECORD_BY_RECORD_NUMBER,
    USER_DEF_MR_DTC_BY_STATUS_MASK,
    USER_DEF_MR_DTC_SNAP_REC_BY_DTC,
    USER_DEF_MR_DTC_EXT_DATA_REC_BY_DTC
};

// UDS query response for each ECU
struct UDSDTCInfo
{
    std::vector<uint8_t> dtcBuffer;
    int32_t targetAddress{ 0 };
};

// Response format of UDS query for async requests
struct DTCResponse
{
    // Negative number means an error by processing (e.g. no response)
    // Value 1 means successful processing
    int8_t result{ -1 };
    std::vector<UDSDTCInfo> dtcInfo;
    std::string token;
};

// Callback format for UDS query results processing
using UDSResponseCallback = std::function<void( const DTCResponse &response )>;

// This class is the interface for UDS queries
class IRemoteDiagnostics
{
public:
    virtual ~IRemoteDiagnostics() = default;

    /**
     * @brief Asynchronously reads Diagnostic Trouble Code (DTC) information.
     *
     * @param targetAddress The target ECU address to query.
     * @param subfn The UDS subfunction to use for the DTC read operation.
     * @param mask The status mask for filtering DTCs.
     * @param callback Function to be called when the operation completes.
     * @param token Unique identifier for this query.
     */
    virtual void readDTCInfo( int32_t targetAddress,
                              UDSSubFunction subfn,
                              UDSStatusMask mask,
                              UDSResponseCallback callback,
                              const std::string &token ) = 0;
    /**
     * @brief Asynchronously reads Diagnostic Trouble Code (DTC) snapshot or extended data
     * information.
     *
     * @param targetAddress The target ECU address to query.
     * @param subfn The UDS subfunction to use for the DTC read operation.
     * @param dtc The specific Diagnostic Trouble Code to query.
     * @param recordNumber The record number associated with the DTC.
     * @param callback Function to be called when the operation completes.
     * @param token Unique identifier for this query.
     */
    virtual void readDTCInfoByDTCAndRecordNumber( int32_t targetAddress,
                                                  UDSSubFunction subfn,
                                                  uint32_t dtc,
                                                  uint8_t recordNumber,
                                                  UDSResponseCallback callback,
                                                  const std::string &token ) = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
