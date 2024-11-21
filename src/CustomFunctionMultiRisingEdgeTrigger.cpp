// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionMultiRisingEdgeTrigger.h"
#include "LoggingModule.h"
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

CustomFunctionMultiRisingEdgeTrigger::CustomFunctionMultiRisingEdgeTrigger(
    std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
    std::shared_ptr<RawData::BufferManager> rawBufferManager )
    : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mRawBufferManager( std::move( rawBufferManager ) )
{
}

CustomFunctionInvokeResult
CustomFunctionMultiRisingEdgeTrigger::invoke( CustomFunctionInvocationID invocationId,
                                              const std::vector<InspectionValue> &args )
{
    if ( ( args.size() < 2 ) || ( ( args.size() % 2 ) != 0 ) )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    auto invocationState = mInvocationStates.find( invocationId );
    if ( invocationState == mInvocationStates.end() ) // First invocation
    {
        InvocationState state;
        state.lastConditionValues.resize( args.size() / 2 );
        for ( size_t i = 0; i < args.size(); i += 2 )
        {
            if ( ( !args[i].isString() ) || ( ( !args[i + 1].isUndefined() ) && ( !args[i + 1].isBoolOrDouble() ) ) )
            {
                return ExpressionErrorCode::TYPE_MISMATCH;
            }
            // coverity[check_return] False positive, this result does not need checking
            state.lastConditionValues[i / 2] = args[i + 1].isUndefined() || args[i + 1].asBool();
        }
        mInvocationStates.emplace( invocationId, std::move( state ) );
        return { ExpressionErrorCode::SUCCESSFUL, false };
    }

    if ( invocationState->second.lastConditionValues.size() != ( args.size() / 2 ) )
    {
        // Number of arguments has somehow changed since the first invocation.
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    bool atLeastOneRisingEdge = false;
    for ( size_t i = 0; i < args.size(); i += 2 )
    {
        if ( ( !args[i].isString() ) || ( ( !args[i + 1].isUndefined() ) && ( !args[i + 1].isBoolOrDouble() ) ) )
        {
            // Type of arguments has somehow changed since the first invocation
            return ExpressionErrorCode::TYPE_MISMATCH;
        }

        auto currentValue = args[i + 1].isUndefined() || args[i + 1].asBool();
        if ( ( !args[i + 1].isUndefined() ) && currentValue &&
             ( !invocationState->second.lastConditionValues[i / 2] ) ) // Rising edge
        {
            atLeastOneRisingEdge = true;
            mTriggeredConditions.push_back( *args[i].stringVal );
        }
        invocationState->second.lastConditionValues[i / 2] = currentValue;
    }
    return { ExpressionErrorCode::SUCCESSFUL, atLeastOneRisingEdge };
}

void
CustomFunctionMultiRisingEdgeTrigger::conditionEnd( const std::unordered_set<SignalID> &collectedSignalIds,
                                                    Timestamp timestamp,
                                                    CollectionInspectionEngineOutput &output )
{
    // Only add to the collected data if we have a valid value:
    if ( mTriggeredConditions.empty() )
    {
        return;
    }
    // Clear the current value:
    auto triggeredConditions = std::move( mTriggeredConditions );
    // Only add to the collected data if collection was triggered:
    if ( !output.triggeredCollectionSchemeData )
    {
        return;
    }
    if ( ( mRawBufferManager == nullptr ) || ( mNamedSignalDataSource == nullptr ) )
    {
        FWE_LOG_WARN( "namedSignalInterface missing from config or raw buffer manager disabled" );
        return;
    }
    auto signalId = mNamedSignalDataSource->getNamedSignalID( "Vehicle.MultiRisingEdgeTrigger" );
    if ( signalId == INVALID_SIGNAL_ID )
    {
        FWE_LOG_WARN( "Vehicle.MultiRisingEdgeTrigger not present in decoder manifest" );
        return;
    }
    if ( collectedSignalIds.find( signalId ) == collectedSignalIds.end() )
    {
        return;
    }
    Json::Value root = Json::arrayValue;
    for ( const auto &conditionName : triggeredConditions )
    {
        root.append( conditionName );
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    auto jsonString = Json::writeString( builder, root );
    auto bufferHandle = mRawBufferManager->push(
        reinterpret_cast<const uint8_t *>( jsonString.data() ), jsonString.size(), timestamp, signalId );
    if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
    {
        return;
    }
    // immediately set usage hint so buffer handle does not get directly deleted again
    mRawBufferManager->increaseHandleUsageHint(
        signalId, bufferHandle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD );
    output.triggeredCollectionSchemeData->signals.emplace_back(
        CollectedSignal{ signalId, timestamp, bufferHandle, SignalType::STRING } );
}

void
CustomFunctionMultiRisingEdgeTrigger::cleanup( CustomFunctionInvocationID invocationId )
{
    mInvocationStates.erase( invocationId );
}

} // namespace IoTFleetWise
} // namespace Aws
