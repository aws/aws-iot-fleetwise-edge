// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CustomFunctionMath.h"
#include <algorithm>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdlib>

namespace Aws
{
namespace IoTFleetWise
{
namespace CustomFunctionMath
{

CustomFunctionInvokeResult
absFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 1 )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( !args[0].isBoolOrDouble() )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    return { ExpressionErrorCode::SUCCESSFUL, std::abs( args[0].asDouble() ) };
}

CustomFunctionInvokeResult
minFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() < 2 )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    double minimum = DBL_MAX;
    for ( const auto &arg : args )
    {
        if ( arg.isUndefined() )
        {
            return ExpressionErrorCode::SUCCESSFUL; // Undefined result
        }
        if ( !arg.isBoolOrDouble() )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        minimum = std::min( minimum, arg.asDouble() );
    }
    return { ExpressionErrorCode::SUCCESSFUL, minimum };
}

CustomFunctionInvokeResult
maxFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() < 2 )
    {
        return { ExpressionErrorCode::TYPE_MISMATCH };
    }
    double maximum = DBL_MIN;
    for ( const auto &arg : args )
    {
        if ( arg.isUndefined() )
        {
            return ExpressionErrorCode::SUCCESSFUL; // Undefined result
        }
        if ( !arg.isBoolOrDouble() )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        maximum = std::max( maximum, arg.asDouble() );
    }
    return { ExpressionErrorCode::SUCCESSFUL, maximum };
}

CustomFunctionInvokeResult
powFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 2 )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() || args[1].isUndefined() )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( ( !args[0].isBoolOrDouble() ) || ( !args[1].isBoolOrDouble() ) )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    // coverity[misra_cpp_2008_rule_19_3_1_violation] errno is used by `pow` to indicate a domain error
    // coverity[autosar_cpp14_m19_3_1_violation] errno is used by `pow` to indicate a domain error
    errno = 0;
    // coverity[autosar_cpp14_a0_4_4_violation] Range errors are detected via errno
    auto powRes = std::pow( args[0].asDouble(), args[1].asDouble() );
    // coverity[misra_cpp_2008_rule_19_3_1_violation] errno is used by `pow` to indicate a domain error
    // coverity[autosar_cpp14_m19_3_1_violation] errno is used by `pow` to indicate a domain error
    if ( errno != 0 )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    return { ExpressionErrorCode::SUCCESSFUL, powRes };
}

CustomFunctionInvokeResult
logFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 2 )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() || args[1].isUndefined() )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( ( !args[0].isBoolOrDouble() ) || ( !args[1].isBoolOrDouble() ) )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    // coverity[misra_cpp_2008_rule_19_3_1_violation] errno is used by `log` to indicate a domain error
    // coverity[autosar_cpp14_m19_3_1_violation] errno is used by `log` to indicate a domain error
    errno = 0;
    auto logBase = std::log( args[0].asDouble() );
    // coverity[misra_cpp_2008_rule_19_3_1_violation] errno is used by `log` to indicate a domain error
    // coverity[autosar_cpp14_m19_3_1_violation] errno is used by `log` to indicate a domain error
    if ( errno != 0 )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    // coverity[misra_cpp_2008_rule_19_3_1_violation] errno is used by `log` to indicate a domain error
    // coverity[autosar_cpp14_m19_3_1_violation] errno is used by `log` to indicate a domain error
    errno = 0;
    auto logNum = std::log( args[1].asDouble() );
    // coverity[misra_cpp_2008_rule_19_3_1_violation] errno is used by `log` to indicate a domain error
    // coverity[autosar_cpp14_m19_3_1_violation] errno is used by `log` to indicate a domain error
    if ( errno != 0 )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    return { ExpressionErrorCode::SUCCESSFUL, logNum / logBase };
}

CustomFunctionInvokeResult
ceilFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 1 )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( !args[0].isBoolOrDouble() )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    return { ExpressionErrorCode::SUCCESSFUL, std::ceil( args[0].asDouble() ) };
}

CustomFunctionInvokeResult
floorFunc( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 1 )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( !args[0].isBoolOrDouble() )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    return { ExpressionErrorCode::SUCCESSFUL, std::floor( args[0].asDouble() ) };
}

} // namespace CustomFunctionMath
} // namespace IoTFleetWise
} // namespace Aws
