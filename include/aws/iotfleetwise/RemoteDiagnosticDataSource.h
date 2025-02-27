// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/IRemoteDiagnostics.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/*
 * Uds query response data format: snapshot and code
 */
struct UdsDtcAndSnapshot
{
    uint32_t dtc{ 0 };
    int32_t recordID{ -1 };
    std::string snapshot;
    std::string extendedData;
};

/*
 * Uds query response data format. Multiple codes can be captured per ECU.
 */
struct UdsDtcInfo
{
    int32_t ecuID{ 0 };
    uint8_t statusAvailabilityMask{ 0 };
    std::vector<UdsDtcAndSnapshot> capturedDTCData;
};

/*
 * Uds query format submitted via DTC_QUERY_FUNCTION. One query can result in multiples sequential requests.
 * When pendingQueries is 0, the query is considered complete.
 */
struct UdsQueryData
{
    FetchRequestID fetchRequestID{ DEFAULT_FETCH_REQUEST_ID };
    std::string signalName;

    std::vector<struct UdsDtcInfo> queryResults;
    uint32_t pendingQueries{ 0 };
};

/*
 * Parameters associated with each UDS query
 */
struct UdsQueryRequestParameters
{
    int32_t ecuID{ 0 };
    UDSSubFunction subFn{ UDSSubFunction::NO_DTC_BY_STATUS_MASK };
    UDSStatusMask stMask{ UDSStatusMask::CONFIRMED_DTC };
    int32_t dtc{ -1 };
    int32_t recordNumber{ -1 };
};

class RemoteDiagnosticDataSource
{
public:
    RemoteDiagnosticDataSource( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                RawData::BufferManager *rawDataBufferManager,
                                std::shared_ptr<IRemoteDiagnostics> diagnosticInterface = nullptr );
    ~RemoteDiagnosticDataSource();

    RemoteDiagnosticDataSource( const RemoteDiagnosticDataSource & ) = delete;
    RemoteDiagnosticDataSource &operator=( const RemoteDiagnosticDataSource & ) = delete;
    RemoteDiagnosticDataSource( RemoteDiagnosticDataSource && ) = delete;
    RemoteDiagnosticDataSource &operator=( RemoteDiagnosticDataSource && ) = delete;

    /**
     * @brief Executes a dtc query based on the provided UDS parameters.
     *
     * This function retrieves active DTCs, snapshot records,
     * and extended data from the ECUs.
     *
     * @param receivedSignalID The ID of the signal that is being queried.
     * @param fetchRequestID The ID associated with the fetch request.
     * @param params A vector of inspection values used in the query.
     *
     * @return FetchErrorCode Indicates the success or failure of the query.
     */
    FetchErrorCode DTC_QUERY( SignalID receivedSignalID,
                              FetchRequestID fetchRequestID,
                              const std::vector<InspectionValue> &params );

    // Start the  thread
    bool start();
    // Stop the  thread
    bool stop();

    /**
     * @brief Returns the health state of the cyclic thread
     * @return True if successful. False otherwise.
     */
    bool isAlive();

    /**
     * @brief Pushes a snapshot JSON to the raw data buffer manager.
     *
     * Pushes a snapshot JSON string to the raw data buffer manager for storage.
     *
     * @param signalName Name of the signal associated with the snapshot.
     * @param fetchRequestID ID of the fetch request associated with the snapshot.
     * @param jsonString Reference to a std::string containing the JSON data.
     */
    void pushSnapshotJsonToRawDataBufferManager( const std::string &signalName,
                                                 FetchRequestID fetchRequestID,
                                                 const std::string &jsonString );

    /**
     * @brief Processes the response to an UDS interface query.
     *
     * This function handles the response received asynchronously from an UDS interface.
     *
     * @param response Structure containing the asynchronous response data.
     *             It includes information such as error codes, diagnostic data, and status.
     *             This parameter must not be modified by the caller.
     *             Ensure that the memory associated with `resp` is not freed during the function call.
     */
    void processUDSQueryResponse( const DTCResponse &response );

private:
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    // Intercepts stop signals.
    bool shouldStop() const;
    // Main worker function. The following operations are coded by the function
    void doWork();

    /**
     * @brief Converts data to JSON format.
     *
     * Converts the provided vector of UdsDtcInfo objects to JSON format using Json::Value.
     *
     * @param queryResults Vector of UdsDtcInfo objects to convert to JSON.
     * @return Json::Value containing the converted JSON data.
     */
    static Json::Value convertDataToJson( const std::vector<UdsDtcInfo> &queryResults );

    /**
     * @brief Converts a number to a hexadecimal string representation.
     *
     * @param value a number to convert
     * @param width width of the hexadecimal string
     * @return std::string A string containing the hexadecimal representation of the input number.
     *
     */
    static std::string toHexString( uint32_t value, int width );

    /**
     * @brief Generates a random string of specified length.
     *
     * Generates a random string of the specified length using alphanumeric characters.
     *
     * @param length Length of the random string to generate.
     * @return The generated random string.
     */
    static std::string generateRandomString( int length );
    /**
     * @brief Processes a DTC query request.
     *
     * Processes a DTC query request with the given parameters and returns an error code.
     *
     * @param parentQueryID Hash string if the original query
     * @param requestParameters Request parameters incl ecuID, subfunction, status mask, dtc and recordID
     * @return FetchErrorCode indicating the result of the query.
     */
    FetchErrorCode processDtcQueryRequest( const std::string &parentQueryID,
                                           const UdsQueryRequestParameters &requestParameters );
    /**
     * @brief Processes a DTC snapshot query request.
     *
     * Processes a DTC snapshot query request with the given parameters and returns an error code.
     *
     * @param parentQueryID Hash string of the original query.
     * @param requestParameters Request parameters incl ecuID, subfunction, status mask, dtc and recordID
     * @return FetchErrorCode indicating the result of the query.
     */
    FetchErrorCode processDtcSnapshotQueryRequest( const std::string &parentQueryID,
                                                   const UdsQueryRequestParameters &requestParameters );

    /**
     * @brief Converts byte data into a string representation.
     *
     * Converts a vector of bytes to a string and stores the result in byteString.
     * Overloaded to support both array and vector input.
     *
     * @param bytes Reference to a std::vector<uint8_t> containing bytes.
     * @param byteString Reference to a std::string where the result is stored.
     */
    static void convertBytesToString( const std::vector<uint8_t> &bytes, std::string &byteString );

    /**
     * @brief Processes raw DTC query results received from the remote diagnostics interface.
     *
     * This function takes raw DTC query results and processes them into a structured format.
     * It handles the conversion of raw data into UdsDtcInfo objects depending on requested subfunction, which represent
     * Diagnostic Trouble Codes (DTCs) and their associated information.
     *
     * @param ecuID Received ECU ID
     * @param rawDTC A vector of bytes containing the raw DTC query results.
     * @param queryParameters Request parameters incl ecuID, subfunction, status mask, dtc and recordID
     * @param queryResults Reference to a vector of UdsDtcInfo objects where the processed results are stored.
     *
     */
    static void processRawDTCQueryResults( const int32_t ecuID,
                                           const std::vector<uint8_t> &rawDTC,
                                           const UdsQueryRequestParameters &queryParameters,
                                           std::vector<struct UdsDtcInfo> &queryResults );

    /**
     * @brief Extracts an 8-bit unsigned integer value from a byte buffer.
     *
     * @param buffer The source vector of bytes.
     * @param index The position in the buffer to extract the value from.
     * @param result Reference to store the extracted value.
     * @return bool True if extraction was successful, false otherwise.
     */
    static bool extractUint8Value( const std::vector<uint8_t> &buffer, size_t index, uint8_t &result );

    /**
     * @brief Extracts a Diagnostic Trouble Code (DTC) from a byte buffer.
     *
     * @param buffer The source vector of bytes containing the DTC.
     * @param index The starting position in the buffer to extract the DTC from.
     * @param result Reference to store the extracted DTC as a 32-bit unsigned integer.
     * @return bool True if extraction was successful, false otherwise.
     */
    static bool extractDtc( const std::vector<uint8_t> &buffer, size_t index, uint32_t &result );

    /**
     * @brief Removes a query from the system based on its ID.
     *
     * This function removes a previously queued query identified by its unique queryID.
     * It's used to clean up or cancel ongoing queries that are no longer needed.
     *
     * @param queryID A string representing the unique identifier of the query to be removed.
     */
    void removeQuery( const std::string &queryID );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;

    Signal mWait;

    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    RawData::BufferManager *mRawDataBufferManager;

    // mutex for below maps
    mutable std::mutex mQueryMapMutex;
    // Map containing requests originally received through DTC_QUERY function and related information
    std::unordered_map<std::string, UdsQueryData> mQueuedDTCQueries;
    // Map of all requests sent to remote diagnostics interface
    std::unordered_map<std::string, UdsQueryRequestParameters> mQueryRequestParameters;
    // Look-up table for sequential requests to find the original query stored in mQueuedDTCQueries
    std::unordered_map<std::string, std::string> mQueryLookup;

    std::vector<std::string> mSignalNames{ "Vehicle.ECU1.DTC_INFO", "Vehicle.ECU2.DTC_INFO", "Vehicle.ECU3.DTC_INFO" };

    std::shared_ptr<IRemoteDiagnostics> mDiagnosticInterface;
};

} // namespace IoTFleetWise
} // namespace Aws
