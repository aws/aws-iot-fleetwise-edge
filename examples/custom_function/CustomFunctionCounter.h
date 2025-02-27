// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <unordered_map>
#include <vector>

class CustomFunctionCounter
{
public:
    Aws::IoTFleetWise::CustomFunctionInvokeResult invoke( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
                                                          const std::vector<Aws::IoTFleetWise::InspectionValue> &args );
    void cleanup( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId );

private:
    std::unordered_map<Aws::IoTFleetWise::CustomFunctionInvocationID, int> mCounters;
};
