// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionFileSize.h"
#include <aws/iotfleetwise/LoggingModule.h>
#include <boost/filesystem.hpp>
#include <exception>
#include <stdexcept>
#include <utility>

CustomFunctionFileSize::CustomFunctionFileSize(
    std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource )
    : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
{
    if ( mNamedSignalDataSource == nullptr )
    {
        throw std::runtime_error( "namedSignalInterface is not configured" );
    }
}

Aws::IoTFleetWise::CustomFunctionInvokeResult
CustomFunctionFileSize::invoke( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
                                const std::vector<Aws::IoTFleetWise::InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( ( args.size() != 1 ) || ( !args[0].isString() ) )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
    }
    try
    {
        boost::filesystem::path filePath( *args[0].stringVal );
        mFileSize = static_cast<int>( boost::filesystem::file_size( filePath ) );
    }
    catch ( const std::exception &e )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
    }
    return { Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL, mFileSize };
}

void
CustomFunctionFileSize::conditionEnd( const std::unordered_set<Aws::IoTFleetWise::SignalID> &collectedSignalIds,
                                      Aws::IoTFleetWise::Timestamp timestamp,
                                      Aws::IoTFleetWise::CollectionInspectionEngineOutput &output )
{
    // Only add to the collected data if we have a valid value:
    if ( mFileSize < 0 )
    {
        return;
    }
    // Clear the current value:
    auto size = mFileSize;
    mFileSize = -1;
    // Only add to the collected data if collection was triggered:
    if ( !output.triggeredCollectionSchemeData )
    {
        return;
    }
    auto signalId = mNamedSignalDataSource->getNamedSignalID( "Vehicle.FileSize" );
    if ( signalId == Aws::IoTFleetWise::INVALID_SIGNAL_ID )
    {
        FWE_LOG_WARN( "Vehicle.FileSize not present in decoder manifest" );
        return;
    }
    if ( collectedSignalIds.find( signalId ) == collectedSignalIds.end() )
    {
        return;
    }
    output.triggeredCollectionSchemeData->signals.emplace_back(
        Aws::IoTFleetWise::CollectedSignal{ signalId, timestamp, size, Aws::IoTFleetWise::SignalType::DOUBLE } );
}
