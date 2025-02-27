// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/RemoteDiagnosticDataSource.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <memory>
#include <ostream>
#include <random>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

RemoteDiagnosticDataSource::RemoteDiagnosticDataSource( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                                        RawData::BufferManager *rawDataBufferManager,
                                                        std::shared_ptr<IRemoteDiagnostics> diagnosticInterface )
    : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mRawDataBufferManager( rawDataBufferManager )
    , mDiagnosticInterface( std::move( diagnosticInterface ) )
{
}

void
RemoteDiagnosticDataSource::pushSnapshotJsonToRawDataBufferManager( const std::string &signalName,
                                                                    FetchRequestID fetchRequestID,
                                                                    const std::string &jsonString )
{
    if ( ( mRawDataBufferManager == nullptr ) || ( mNamedSignalDataSource == nullptr ) )
    {
        FWE_LOG_WARN( "Raw message for signal name " + signalName + " can not be handed over to RawBufferManager" );
        return;
    }

    auto signalID = mNamedSignalDataSource->getNamedSignalID( signalName );
    if ( signalID == INVALID_SIGNAL_ID )
    {
        FWE_LOG_TRACE( "No decoding rules set for signal name " + signalName );
        return;
    }

    auto receiveTime = mClock->systemTimeSinceEpochMs();
    std::vector<uint8_t> buffer( jsonString.begin(), jsonString.end() );
    auto bufferHandle = mRawDataBufferManager->push( ( buffer.data() ), buffer.size(), receiveTime, signalID );

    if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
    {
        FWE_LOG_WARN( "Raw message id: " + std::to_string( signalID ) + "  was rejected by RawBufferManager" );
        return;
    }
    // immediately set usage hint so buffer handle does not get directly deleted again
    mRawDataBufferManager->increaseHandleUsageHint(
        signalID, bufferHandle, RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );

    mNamedSignalDataSource->ingestSignalValue(
        receiveTime, signalName, DecodedSignalValue{ bufferHandle, SignalType::STRING }, fetchRequestID );
}

bool
RemoteDiagnosticDataSource::start()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "Remote Diagnostics Module Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Remote Diagnostics Module Thread started" );
        mThread.setThreadName( "fwDIUDSRD" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
RemoteDiagnosticDataSource::stop()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return true;
    }

    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();

    FWE_LOG_TRACE( "Remote Diagnostics Module Thread requested to stop" );
    mThread.release();

    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Thread stopped" );
    return !mThread.isActive();
}

bool
RemoteDiagnosticDataSource::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
RemoteDiagnosticDataSource::isAlive()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return false;
    }

    return true;
}

void
RemoteDiagnosticDataSource::doWork()
{
    while ( !shouldStop() )
    {
        mWait.wait( Signal::WaitWithPredicate );

        std::lock_guard<std::mutex> lock( mQueryMapMutex );
        auto it = mQueuedDTCQueries.begin();
        while ( it != mQueuedDTCQueries.end() )
        {
            if ( it->second.pendingQueries == 0 )
            {
                // Create Json
                if ( !it->second.queryResults.empty() )
                {
                    Json::Value root = convertDataToJson( it->second.queryResults );
                    Json::StreamWriterBuilder builder;
                    builder["indentation"] = "";

                    std::string jsonString = Json::writeString( builder, root );

                    FWE_LOG_TRACE( "Retrieved DTCs and Snapshot: " + jsonString );
                    pushSnapshotJsonToRawDataBufferManager(
                        it->second.signalName, it->second.fetchRequestID, jsonString );
                }
                auto queryID = it->first;
                it = mQueuedDTCQueries.erase( it );
                // Erase from all maps
                mQueuedDTCQueries.erase( queryID );
            }
            else
            {
                ++it;
            }
        }
    }
}

FetchErrorCode
RemoteDiagnosticDataSource::DTC_QUERY( SignalID receivedSignalID,
                                       FetchRequestID fetchRequestID,
                                       const std::vector<InspectionValue> &params )
{
    if ( ( shouldStop() ) || ( !isAlive() ) )
    {
        // Don't process request if thread is shutting down
        return FetchErrorCode::REQUESTED_TO_STOP;
    }

    if ( ( mNamedSignalDataSource == nullptr ) || ( mDiagnosticInterface == nullptr ) )
    {
        FWE_LOG_ERROR( "DTC Query request cannot be processed" );
        return FetchErrorCode::SIGNAL_NOT_FOUND;
    }

    UdsQueryData udsQuery;
    UdsQueryRequestParameters udsQueryRequestParameters;

    bool found = false;
    for ( const auto &it : mSignalNames )
    {
        auto signalID = mNamedSignalDataSource->getNamedSignalID( it );
        if ( ( signalID == INVALID_SIGNAL_ID ) || ( signalID != receivedSignalID ) )
        {
            continue;
        }
        udsQuery.signalName = it;
        udsQuery.fetchRequestID = fetchRequestID;
        found = true;
        break;
    }

    if ( !found )
    {
        FWE_LOG_TRACE( "No decoding rules set for signal id " + std::to_string( receivedSignalID ) );
        return FetchErrorCode::SIGNAL_NOT_FOUND;
    }

    // Ensure the program receives a minimum of three parameters to proceed with request processing;
    // otherwise, terminate further execution.
    if ( params.size() < 3 )
    {
        FWE_LOG_ERROR( "Invalid parameters received." );
        return FetchErrorCode::UNSUPPORTED_PARAMETERS;
    }

    // Parse incoming request parameters
    // Parse target address
    if ( !params[0].isBoolOrDouble() )
    {
        FWE_LOG_ERROR( "Target address parameter must be of type double" );
        return FetchErrorCode::UNSUPPORTED_PARAMETERS;
    }
    udsQueryRequestParameters.ecuID = static_cast<int>( params[0].asDouble() );

    // Parse subfunction
    if ( !params[1].isBoolOrDouble() )
    {
        FWE_LOG_ERROR( "Subfunction parameter must be of type double" );
        return FetchErrorCode::UNSUPPORTED_PARAMETERS;
    }
    int subFnInt = static_cast<int>( params[1].asDouble() );
    if ( !params[2].isBoolOrDouble() )
    {
        FWE_LOG_ERROR( "Status mask parameter must be of type double" );
        return FetchErrorCode::UNSUPPORTED_PARAMETERS;
    }
    if ( ( subFnInt < static_cast<int>( UDSSubFunction::NO_DTC_BY_STATUS_MASK ) ) ||
         ( subFnInt > static_cast<int>( UDSSubFunction::USER_DEF_MR_DTC_EXT_DATA_REC_BY_DTC ) ) )
    {
        FWE_LOG_ERROR( "Invalid parameters received. UDS subfunction parameter is out of range." );
        return FetchErrorCode::UNSUPPORTED_PARAMETERS;
    }
    udsQueryRequestParameters.subFn = static_cast<UDSSubFunction>( subFnInt );

    // Parse status mask
    int stMaskInt = static_cast<int>( params[2].asDouble() );
    if ( ( ( stMaskInt <= 0 ) && ( stMaskInt != -1 ) ) || ( stMaskInt >= 0xFF ) )
    {
        FWE_LOG_ERROR( "Invalid parameters received. UDS status mask parameter is out of range." );
        return FetchErrorCode::UNSUPPORTED_PARAMETERS;
    }
    if ( stMaskInt > -1 )
    {
        // coverity[autosar_cpp14_a7_2_1_violation] false-positive, check is done above, values are inside of range
        udsQueryRequestParameters.stMask = static_cast<UDSStatusMask>( stMaskInt );
    }

    // Parse DTC
    if ( params.size() >= 4 )
    {
        if ( !params[3].isString() )
        {
            FWE_LOG_ERROR( "Invalid DTC code received. Parameter should be of type string." );
            return FetchErrorCode::UNSUPPORTED_PARAMETERS;
        }
        std::string dtcString = *params[3].stringVal;
        if ( ( !dtcString.empty() ) && ( dtcString != "-1" ) )
        {
            try
            {
                udsQueryRequestParameters.dtc = std::stoi( dtcString, nullptr, 0 );
            }
            catch ( ... )
            {
                FWE_LOG_ERROR( "Could not convert received DTC code parameter to int " + dtcString );
                return FetchErrorCode::UNSUPPORTED_PARAMETERS;
            }
        }
    }

    // Parse record number
    if ( params.size() >= 5 )
    {
        if ( !params[4].isBoolOrDouble() )
        {
            FWE_LOG_ERROR( "Record number must be of type double" );
            return FetchErrorCode::UNSUPPORTED_PARAMETERS;
        }
        udsQueryRequestParameters.recordNumber = static_cast<int>( params[4].asDouble() );
    }

    std::stringstream ss;
    ss << std::uppercase << std::hex << udsQueryRequestParameters.dtc;
    FWE_LOG_INFO( "Received Parameters are: Target Address: " + std::to_string( udsQueryRequestParameters.ecuID ) +
                  " subfn: " + std::to_string( static_cast<int>( udsQueryRequestParameters.subFn ) ) +
                  ",stMask: " + std::to_string( static_cast<int>( udsQueryRequestParameters.stMask ) ) + " dtc " +
                  ss.str() + " recordNumber " + std::to_string( udsQueryRequestParameters.recordNumber ) );

    std::string udsQueryID = generateRandomString( 24 );

    std::lock_guard<std::mutex> lock( mQueryMapMutex );
    mQueuedDTCQueries.emplace( udsQueryID, udsQuery );
    mQueryRequestParameters.emplace( udsQueryID, udsQueryRequestParameters );
    mQueryLookup.emplace( udsQueryID, udsQueryID );

    if ( udsQueryRequestParameters.subFn == UDSSubFunction::DTC_BY_STATUS_MASK )
    {
        return processDtcQueryRequest( udsQueryID, udsQueryRequestParameters );
    }
    else if ( ( udsQueryRequestParameters.subFn == UDSSubFunction::DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER ) ||
              ( udsQueryRequestParameters.subFn == UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER ) )
    {
        // If DTC code is not given, first query DTC codes
        if ( udsQueryRequestParameters.dtc <= 0 )
        {
            UdsQueryRequestParameters sequentialRequestParameters = udsQueryRequestParameters;
            sequentialRequestParameters.subFn = UDSSubFunction::DTC_BY_STATUS_MASK;
            return processDtcQueryRequest( udsQueryID, sequentialRequestParameters );
        }

        // If record number is not given, query for it
        if ( udsQueryRequestParameters.recordNumber <= 0 )
        {
            UdsQueryRequestParameters sequentialRequestParameters = udsQueryRequestParameters;
            sequentialRequestParameters.subFn = UDSSubFunction::DTC_SNAPSHOT_IDENTIFICATION;
            return processDtcQueryRequest( udsQueryID, sequentialRequestParameters );
        }

        if ( udsQueryRequestParameters.ecuID == -1 )
        {
            FWE_LOG_ERROR( "Unsupported target address format encountered: Unable to process all address(-1) "
                           "as specified" );
            return FetchErrorCode::UNSUPPORTED_PARAMETERS;
        }

        return processDtcSnapshotQueryRequest( udsQueryID, udsQueryRequestParameters );
    }
    return FetchErrorCode::NOT_IMPLEMENTED;
}

FetchErrorCode
RemoteDiagnosticDataSource::processDtcQueryRequest( const std::string &parentQueryID,
                                                    const UdsQueryRequestParameters &requestParameters )
{
    if ( ( shouldStop() ) || ( !isAlive() ) )
    {
        // Don't process request if thread is shutting down
        return FetchErrorCode::REQUESTED_TO_STOP;
    }
    std::string newQueryID = generateRandomString( 24 );
    FWE_LOG_TRACE( "Sending DTC query request " + newQueryID + " for original request " + parentQueryID +
                   " for target address " + std::to_string( requestParameters.ecuID ) + ", subfunction " +
                   std::to_string( static_cast<int>( requestParameters.subFn ) ) ) +
        ", status mask " + std::to_string( static_cast<int>( requestParameters.stMask ) );

    mQueuedDTCQueries[parentQueryID].pendingQueries += 1;
    mQueryLookup.emplace( newQueryID, parentQueryID );
    mQueryRequestParameters.emplace( newQueryID, requestParameters );

    mDiagnosticInterface->readDTCInfo(
        requestParameters.ecuID,
        requestParameters.subFn,
        requestParameters.stMask,
        std::bind( &RemoteDiagnosticDataSource::processUDSQueryResponse, this, std::placeholders::_1 ),
        newQueryID );
    return FetchErrorCode::SUCCESSFUL;
}

FetchErrorCode
RemoteDiagnosticDataSource::processDtcSnapshotQueryRequest( const std::string &parentQueryID,
                                                            const UdsQueryRequestParameters &requestParameters )
{
    if ( ( shouldStop() ) || ( !isAlive() ) )
    {
        // Don't process request if thread is shutting down
        return FetchErrorCode::REQUESTED_TO_STOP;
    }
    std::string newQueryID = generateRandomString( 24 );
    FWE_LOG_TRACE( "Sending DTC snapshot query request " + newQueryID + " for original request " + parentQueryID +
                   " for target address " + std::to_string( requestParameters.ecuID ) + ", subfunction " +
                   std::to_string( static_cast<int>( requestParameters.subFn ) ) + ", dtc code " +
                   std::to_string( requestParameters.dtc ) + ", recordNumber " +
                   std::to_string( requestParameters.recordNumber ) );

    mQueuedDTCQueries[parentQueryID].pendingQueries += 1;
    mQueryLookup.emplace( newQueryID, parentQueryID );
    mQueryRequestParameters.emplace( newQueryID, requestParameters );

    mDiagnosticInterface->readDTCInfoByDTCAndRecordNumber(
        requestParameters.ecuID,
        requestParameters.subFn,
        static_cast<uint32_t>( requestParameters.dtc ),
        static_cast<uint8_t>( requestParameters.recordNumber ),
        std::bind( &RemoteDiagnosticDataSource::processUDSQueryResponse, this, std::placeholders::_1 ),
        newQueryID.c_str() );
    return FetchErrorCode::SUCCESSFUL;
}

void
RemoteDiagnosticDataSource::processUDSQueryResponse( const DTCResponse &response )
{
    if ( ( shouldStop() ) || ( !isAlive() ) )
    {
        // Don't process response if thread is shutting down
        return;
    }
    std::string receivedQueryID = std::string( response.token );
    FWE_LOG_TRACE( "Received Response for hash: " + receivedQueryID );
    std::lock_guard<std::mutex> lock( mQueryMapMutex );
    auto requestParametersIt = mQueryRequestParameters.find( receivedQueryID );
    auto originalRequestIDIt = mQueryLookup.find( receivedQueryID );

    if ( requestParametersIt == mQueryRequestParameters.end() || originalRequestIDIt == mQueryLookup.end() )
    {
        FWE_LOG_ERROR( "No associated request found for the received hash " + receivedQueryID );
        return;
    }
    auto originalRequestID = originalRequestIDIt->second;
    auto dtcQuery = mQueuedDTCQueries.find( originalRequestID );
    if ( dtcQuery == mQueuedDTCQueries.end() )
    {
        FWE_LOG_ERROR( "Original request id " + originalRequestID + "does not exist for the received hash " +
                       receivedQueryID );
        removeQuery( originalRequestID );
        removeQuery( receivedQueryID );
        return;
    }
    dtcQuery->second.pendingQueries -= 1;

    auto requestParameters = requestParametersIt->second;
    if ( response.result < 0 )
    {
        FWE_LOG_ERROR( "Diagnostics interface returned an error with the code " + std::to_string( response.result ) +
                       " for " + receivedQueryID + " hash" );
        removeQuery( originalRequestID );
        removeQuery( receivedQueryID );
        return;
    }
    if ( response.dtcInfo.empty() )
    {
        FWE_LOG_ERROR( "Received an empty response from diagnostics interface for " + receivedQueryID + " hash" );
        removeQuery( originalRequestID );
        removeQuery( receivedQueryID );
        return;
    }
    for ( auto &dtcInfoEntry : response.dtcInfo )
    {
        if ( ( !dtcInfoEntry.dtcBuffer.empty() ) &&
             ( ( requestParameters.ecuID == -1 ) || ( dtcInfoEntry.targetAddress == requestParameters.ecuID ) ) )
        {
            // Extract raw data from the response
            FWE_LOG_TRACE( "Received " + std::to_string( dtcInfoEntry.dtcBuffer.size() ) +
                           " bytes for target address " + std::to_string( dtcInfoEntry.targetAddress ) + ": " +
                           getStringFromBytes( dtcInfoEntry.dtcBuffer ) );
            processRawDTCQueryResults(
                dtcInfoEntry.targetAddress, dtcInfoEntry.dtcBuffer, requestParameters, dtcQuery->second.queryResults );
        }
    }
    // Execute sequential requests if required
    auto originalRequestParameters = mQueryRequestParameters.find( originalRequestID );
    if ( originalRequestParameters == mQueryRequestParameters.end() )
    {
        FWE_LOG_ERROR( "No request parameter found for the original request with hash " + originalRequestID );
        removeQuery( originalRequestID );
        removeQuery( receivedQueryID );
        return;
    }
    // Delete sequential query from the lists since it's now fully processed
    removeQuery( receivedQueryID );
    if ( ( originalRequestParameters->second.subFn == UDSSubFunction::DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER ) ||
         ( originalRequestParameters->second.subFn == UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER ) )
    {
        if ( requestParameters.subFn == UDSSubFunction::DTC_BY_STATUS_MASK )
        {
            // Request record id if DTCs were queried before
            UdsQueryRequestParameters sequentialRequestParameters = requestParameters;
            sequentialRequestParameters.subFn = UDSSubFunction::DTC_SNAPSHOT_IDENTIFICATION;

            processDtcQueryRequest( originalRequestID, sequentialRequestParameters );
            return;
        }

        if ( requestParameters.subFn == UDSSubFunction::DTC_SNAPSHOT_IDENTIFICATION )
        {
            for ( auto &queryResult : dtcQuery->second.queryResults )
            {
                for ( auto &capturedDTC : queryResult.capturedDTCData )
                {
                    if ( capturedDTC.recordID <= 0 )
                    {
                        // Skip request if no record ID exist
                        continue;
                    }
                    // Request snapshot data if record id was queried before
                    UdsQueryRequestParameters sequentialRequestParameters = requestParameters;
                    // Query the original request subfunction (snapshot data or extended data)
                    sequentialRequestParameters.subFn = originalRequestParameters->second.subFn;
                    sequentialRequestParameters.ecuID = queryResult.ecuID;
                    sequentialRequestParameters.dtc = static_cast<int>( capturedDTC.dtc );
                    sequentialRequestParameters.recordNumber = capturedDTC.recordID;

                    processDtcSnapshotQueryRequest( originalRequestID, sequentialRequestParameters );
                }
            }
        }
    }
    mWait.notify();
}

void
RemoteDiagnosticDataSource::processRawDTCQueryResults( const int ecuID,
                                                       const std::vector<uint8_t> &rawDTC,
                                                       const UdsQueryRequestParameters &queryParameters,
                                                       std::vector<struct UdsDtcInfo> &queryResults )
{
    // Define a named lambda here to comply with AUTOSAR A5-1-9
    auto findQuery = [&]( UdsDtcInfo &it ) -> bool {
        return it.ecuID == ecuID;
    };

    if ( queryParameters.subFn == UDSSubFunction::DTC_BY_STATUS_MASK )
    {
        UdsDtcInfo queryResult;
        /*  According to ISO 14229-1 Table-272, DTC retrieval involves incrementing by 4.
            The response format of DTC_BY_STATUS_MASK is as follows:
            +---------------------------------------------+
            | SID | reportType | DTCSAM | DTCASR | .....  |
            +---------------------------------------------+
            Since rawDTC contains DTCSAM (DTCStatusAvailabilityMask) followed
            by
        DTCASR (DTCAndStatusRecord[]), where the structure of DTCAndStatusRecord[] is:
        DTCHighByte#N DTCMiddleByte#N DTCLowByte#N StatusOfDTC#N As we are only retrieving DTCs,
        we ignore StatusOfDTC#N. Therefore, we start the index at 1 and increment by +4. */
        if ( !extractUint8Value( rawDTC, 0, queryResult.statusAvailabilityMask ) )
        {
            return;
        }
        for ( size_t j = 1; j < rawDTC.size(); j += 4 )
        {
            uint32_t dtc = 0;
            if ( !extractDtc( rawDTC, j, dtc ) )
            {
                break;
            }
            if ( ( ( queryParameters.dtc <= 0 ) || ( static_cast<uint32_t>( queryParameters.dtc ) == dtc ) ) &&
                 ( dtc != 0 ) )
            {
                UdsDtcAndSnapshot udsDtcAndSnapshot;
                udsDtcAndSnapshot.dtc = dtc;
                udsDtcAndSnapshot.recordID = queryParameters.recordNumber;
                queryResult.ecuID = ecuID;
                queryResult.capturedDTCData.emplace_back( udsDtcAndSnapshot );
            }
        }
        if ( !queryResult.capturedDTCData.empty() )
        {
            queryResults.emplace_back( queryResult );
        }
    }
    else if ( queryParameters.subFn == UDSSubFunction::DTC_SNAPSHOT_IDENTIFICATION )
    {
        /*  According to ISO 14229-1 Table-272, DTC retrieval involves incrementing by 4.
            The response format of DTC_SNAPSHOT_IDENTIFICATION is as follows:
            +------------------------------------------------------+
            | SID | DTCRecord[] | DTCSnapshotRecordNumber | .....  |
            +------------------------------------------------------+
            Since rawDTC contains DTCASR_ (DTCRecord) followed by
            DTCSSRN (DTCSnapshotRecordNumber), As we are only retrieving DTC RecrodNumber,
            we ignore DTCASR_#N. Therefore, we start the index at 1 and increment by +4.
        */
        for ( size_t j = 0; j < rawDTC.size(); j += 4 )
        {
            uint32_t dtc = 0;
            if ( !extractDtc( rawDTC, j, dtc ) )
            {
                break;
            }
            uint8_t record = 0;
            if ( !extractUint8Value( rawDTC, j + 3, record ) )
            {
                break;
            }

            auto queryResult = std::find_if( queryResults.begin(), queryResults.end(), findQuery );
            if ( queryResult == queryResults.end() )
            {
                if ( ( static_cast<uint32_t>( queryParameters.dtc ) == dtc ) &&
                     ( ( queryParameters.ecuID == ecuID ) || ( queryParameters.ecuID == -1 ) ) )
                {
                    UdsDtcInfo newQueryResult;
                    UdsDtcAndSnapshot udsDtcAndSnapshot;
                    udsDtcAndSnapshot.dtc = dtc;
                    udsDtcAndSnapshot.recordID = record;
                    newQueryResult.ecuID = ecuID;
                    newQueryResult.capturedDTCData.emplace_back( udsDtcAndSnapshot );
                    queryResults.emplace_back( newQueryResult );
                }
                return;
            }

            for ( auto &capturedDTCData : queryResult->capturedDTCData )
            {
                if ( capturedDTCData.dtc == dtc )
                {
                    capturedDTCData.recordID = record;
                    break;
                }
            }
        }
    }
    else if ( ( queryParameters.subFn == UDSSubFunction::DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER ) ||
              ( queryParameters.subFn == UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER ) )
    {
        uint32_t dtc = 0;
        if ( !extractDtc( rawDTC, 0, dtc ) )
        {
            return;
        }
        std::string dtcStr = toHexString( dtc, 6 );
        FWE_LOG_INFO( "Snapshot received for " + dtcStr + " DTC" );

        auto queryResult = std::find_if( queryResults.begin(), queryResults.end(), findQuery );
        if ( queryResult == queryResults.end() )
        {
            if ( ( static_cast<uint32_t>( queryParameters.dtc ) == dtc ) &&
                 ( ( queryParameters.ecuID == ecuID ) || ( queryParameters.ecuID == -1 ) ) )
            {
                UdsDtcInfo newQueryResult;
                UdsDtcAndSnapshot udsDtcAndSnapshot;
                udsDtcAndSnapshot.dtc = dtc;
                udsDtcAndSnapshot.recordID = queryParameters.recordNumber;
                newQueryResult.ecuID = ecuID;

                if ( queryParameters.subFn == UDSSubFunction::DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER )
                {
                    convertBytesToString( rawDTC, udsDtcAndSnapshot.snapshot );
                }
                if ( queryParameters.subFn == UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER )
                {
                    convertBytesToString( rawDTC, udsDtcAndSnapshot.extendedData );
                }

                newQueryResult.capturedDTCData.emplace_back( udsDtcAndSnapshot );
                queryResults.emplace_back( newQueryResult );
            }
            return;
        }

        for ( auto &dtcAndSnapshot : queryResult->capturedDTCData )
        {
            if ( dtcAndSnapshot.dtc == dtc )
            {
                if ( queryParameters.subFn == UDSSubFunction::DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER )
                {
                    convertBytesToString( rawDTC, dtcAndSnapshot.snapshot );
                }
                if ( queryParameters.subFn == UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER )
                {
                    convertBytesToString( rawDTC, dtcAndSnapshot.extendedData );
                }
                break;
            }
        }
    }
}

std::string
RemoteDiagnosticDataSource::generateRandomString( int length )
{
    const std::string ASCII_CHARACTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device randomStr;
    std::mt19937 generator( randomStr() );
    std::uniform_int_distribution<> distribution( 0, static_cast<int>( ASCII_CHARACTERS.size() - 1 ) );

    std::string random_string;
    for ( int i = 0; i < length; ++i )
    {
        // Use size_t or std::string::size_type for indexing
        // coverity[cert_str53_cpp_violation] False-positive, distribution is limited by the string size
        random_string += ASCII_CHARACTERS[static_cast<size_t>( distribution( generator ) )];
    }
    return random_string;
}

bool
RemoteDiagnosticDataSource::extractDtc( const std::vector<uint8_t> &buffer, size_t index, uint32_t &result )
{
    if ( index + 3 > buffer.size() )
    {
        FWE_LOG_TRACE( "Received DTC info of invalid size" );
        return false;
    }
    result = ( static_cast<uint32_t>( buffer[index] ) << 16 ) | ( static_cast<uint32_t>( buffer[index + 1] ) << 8 ) |
             ( static_cast<uint32_t>( buffer[index + 2] ) );
    return true;
}

bool
RemoteDiagnosticDataSource::extractUint8Value( const std::vector<uint8_t> &buffer, size_t index, uint8_t &result )
{
    if ( index >= buffer.size() )
    {
        FWE_LOG_TRACE( "Received DTC info of invalid size" );
        return false;
    }
    result = buffer[index];
    return true;
}

void
RemoteDiagnosticDataSource::convertBytesToString( const std::vector<uint8_t> &bytes, std::string &byteString )
{
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill( '0' );
    for ( size_t i = 0; i < bytes.size(); ++i )
    {
        ss << std::setw( 2 ) << static_cast<int>( bytes[i] );
    }
    byteString.assign( ss.str() );
}

std::string
RemoteDiagnosticDataSource::toHexString( uint32_t value, int width )
{
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill( '0' ) << std::setw( width ) << value;
    return ss.str();
}

Json::Value
RemoteDiagnosticDataSource::convertDataToJson( const std::vector<UdsDtcInfo> &queryResults )
{
    Json::Value root;
    Json::Value dataArray( Json::arrayValue );
    for ( auto &queryResult : queryResults )
    {
        Json::Value dtcCodes( Json::arrayValue );

        for ( auto &capturedDTC : queryResult.capturedDTCData )
        {
            Json::Value dtc;
            dtc["DTC"] = "";
            dtc["DTCSnapshotRecord"] = "";
            dtc["DTCExtendedData"] = "";

            if ( capturedDTC.dtc != 0U )
            {
                dtc["DTC"] = toHexString( capturedDTC.dtc, 6 );
            }
            if ( !capturedDTC.snapshot.empty() )
            {
                dtc["DTCSnapshotRecord"] = capturedDTC.snapshot;
            }
            if ( !capturedDTC.extendedData.empty() )
            {
                dtc["DTCExtendedData"] = capturedDTC.extendedData;
            }
            dtcCodes.append( dtc );
        }

        Json::Value dtcSnapshot;
        dtcSnapshot["DTCStatusAvailabilityMask"] = toHexString( queryResult.statusAvailabilityMask, 2 );
        dtcSnapshot["dtcCodes"] = dtcCodes;
        Json::Value element;
        element["ECUID"] = toHexString( static_cast<uint32_t>( queryResult.ecuID ), 2 );
        element["DTCAndSnapshot"] = dtcSnapshot;

        dataArray.append( element );
    }

    root["DetectedDTCs"] = dataArray;

    return root;
}

void
RemoteDiagnosticDataSource::removeQuery( const std::string &queryID )
{
    FWE_LOG_TRACE( "Removing query with ID " + queryID + " from pending list" );
    mQueuedDTCQueries.erase( queryID );
    mQueryRequestParameters.erase( queryID );
    mQueryLookup.erase( queryID );
}

RemoteDiagnosticDataSource::~RemoteDiagnosticDataSource()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
