// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace CustomFunctionMath
{

CustomFunctionInvokeResult absFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args );

CustomFunctionInvokeResult minFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args );

CustomFunctionInvokeResult maxFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args );

CustomFunctionInvokeResult powFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args );

CustomFunctionInvokeResult logFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args );

CustomFunctionInvokeResult ceilFunc( CustomFunctionInvocationID invocationId,
                                     const std::vector<InspectionValue> &args );

CustomFunctionInvokeResult floorFunc( CustomFunctionInvocationID invocationId,
                                      const std::vector<InspectionValue> &args );

} // namespace CustomFunctionMath
} // namespace IoTFleetWise
} // namespace Aws
