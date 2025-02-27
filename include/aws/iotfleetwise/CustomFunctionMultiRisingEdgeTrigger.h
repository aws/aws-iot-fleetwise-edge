// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * MULTI_RISING_EDGE_TRIGGER custom function implementation
 *
 * Custom function signature:
 *
 *     bool custom_function('MULTI_RISING_EDGE_TRIGGER',
 *         string conditionName1, bool condition1,
 *         string conditionName2, bool condition2,
 *         string conditionName3, bool condition3,
 *         ...
 *     );
 *
 * The function takes a variable number of pairs of arguments, with each pair being a string name
 * of the condition and a Boolean value of the condition itself.
 *
 * The function will return true when one or more of the conditions has a rising edge (false ->
 * true). Additionally it will produce a string signal called `Vehicle.MultiTriggerInfo` that
 * contains a JSON serialized array of strings containing the names of the conditions that have a
 * rising edge.
 */
class CustomFunctionMultiRisingEdgeTrigger
{
public:
    CustomFunctionMultiRisingEdgeTrigger( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                          RawData::BufferManager *rawDataBufferManager );

    CustomFunctionInvokeResult invoke( CustomFunctionInvocationID invocationId,
                                       const std::vector<InspectionValue> &args );

    void conditionEnd( const std::unordered_set<SignalID> &collectedSignalIds,
                       Timestamp timestamp,
                       CollectionInspectionEngineOutput &output );

    void cleanup( CustomFunctionInvocationID invocationId );

private:
    struct InvocationState
    {
        // coverity[autosar_cpp14_a18_1_2_violation] std::vector<bool> specialization is acceptable in this usecase
        std::vector<bool> lastConditionValues;
    };
    std::unordered_map<CustomFunctionInvocationID, InvocationState> mInvocationStates;
    std::vector<std::string> mTriggeredConditions;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    RawData::BufferManager *mRawDataBufferManager;
};

} // namespace IoTFleetWise
} // namespace Aws
