// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionCounter.h"
#include <utility>

Aws::IoTFleetWise::CustomFunctionInvokeResult
CustomFunctionCounter::invoke( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
                               const std::vector<Aws::IoTFleetWise::InspectionValue> &args )
{
    static_cast<void>( args );
    // Create a new counter if the invocationId is new, or get the existing counter:
    auto &counter = mCounters.emplace( invocationId, 0 ).first->second;
    return { Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL, counter++ };
}

void
CustomFunctionCounter::cleanup( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId )
{
    mCounters.erase( invocationId );
}
