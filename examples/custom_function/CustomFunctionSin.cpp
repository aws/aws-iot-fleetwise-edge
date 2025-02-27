
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionSin.h"
#include <cmath>

Aws::IoTFleetWise::CustomFunctionInvokeResult
customFunctionSin( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
                   const std::vector<Aws::IoTFleetWise::InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 1 )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( !args[0].isBoolOrDouble() )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
    }
    return { Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL, std::sin( args[0].asDouble() ) };
}
