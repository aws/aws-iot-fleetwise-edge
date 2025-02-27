// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

struct FetchRequest
{
    SignalID signalID{ 0U };
    std::string functionName;
    std::vector<InspectionValue> args;
};

struct LastExecutionInfo
{
    uint64_t lastExecutionMonotonicTimeMs{ 0 };
    // TODO: below parameters are not yet supported by the cloud and are ignored on edge
    uint64_t firstExecutionMonotonicTimeMs{ 0 };
    uint32_t executionCount{ 0 };
};

struct PeriodicalFetchParameters
{
    uint64_t fetchFrequencyMs{ 0U };
    // TODO: below parameters are not yet supported by the cloud and are therefore ignored on edge
    uint64_t maxExecutionCount{ 0U };
    uint64_t maxExecutionCountResetPeriodMs{ 0U };
};

struct FetchMatrix
{
    // Map of all fetch requests that were registered in the collection schemes
    std::unordered_map<FetchRequestID, std::vector<FetchRequest>> fetchRequests;
    // Map of periodical fetch parameters provided in the collection schemes
    std::unordered_map<FetchRequestID, PeriodicalFetchParameters> periodicalFetchRequestSetup;
};

enum class FetchErrorCode
{
    SUCCESSFUL,
    SIGNAL_NOT_FOUND,
    UNSUPPORTED_PARAMETERS,
    REQUESTED_TO_STOP,
    NOT_IMPLEMENTED
};

using CustomFetchFunction =
    std::function<FetchErrorCode( SignalID, FetchRequestID, const std::vector<InspectionValue> & )>;

using FetchRequestQueue = LockedQueue<FetchRequestID>;
} // namespace IoTFleetWise
} // namespace Aws
