// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CustomFunctionScriptEngine.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * CPython custom function implementation
 *
 * Custom function signature:
 *
 *     variant                // Return value of `invoke` function, see below
 *     custom_function(
 *         'python',
 *         string s3Prefix,   // S3 prefix to download the scripts from
 *         string moduleName, // Top-level Python module to import containing the `invoke` function
 *         ...                // Remaining args are passed to `invoke` function
 *     );
 *
 * CPython is the standard implementation of Python 3, and the interpreter configuration for FWE enables all features
 * including filesystem and network access. As such, THIS IMPLEMENTATION SHOULD BE USED FOR TESTING PURPOSES ONLY, AND
 * SHOULD NOT BE USED IN PRODUCTION SYSTEMS.
 *
 * The imported Python module must contain a function called `invoke` and can optionally contain a function called
 * `cleanup`.
 *
 * - The `invoke` function will be called each time the campaign expression is evaluated. The return value can
 *   either be a single primitive result of type `None`, `bool`, `float` or `str` (string), or it can be a tuple with
 *   the first member being the primitive result and the second being a `dict` containing collected data. The keys of
 * the `dict` are fully-qualified-name of signals, and the values of the `dict` must be string values. See below for
 *   examples.
 *
 * - The `cleanup` function is called when the invocation instance is no longer used. Each invocation instance of the
 *   custom function is independent of the others, hence it is possible to use the same Python module multiple times
 *   within the same or different campaigns, and any global variables in the scripts will be independent.
 *
 * Examples:
 *
 * ```python
 * # Example Python custom function to sum the two arguments
 *
 * def invoke(a, b):
 *     return a + b
 * ```
 *
 * ```python
 * # Example Python custom function to return true on a rising edge with collected data of the number of rising edges
 *
 * last_level = True
 * rising_edge_count = 0
 *
 * def invoke(level):
 *     global last_level
 *     global rising_edge_count
 *     rising_edge = level and not last_level
 *     last_level = level
 *     if rising_edge:
 *          rising_edge_count += 1
 *          return True, {'Vehicle.RisingEdgeCount': str(rising_edge_count)}
 *     return False
 * ```
 */
class CustomFunctionCPython
{
public:
    CustomFunctionCPython( std::shared_ptr<CustomFunctionScriptEngine> scriptEngine );
    ~CustomFunctionCPython();

    CustomFunctionCPython( const CustomFunctionCPython & ) = delete;
    CustomFunctionCPython &operator=( const CustomFunctionCPython & ) = delete;
    CustomFunctionCPython( CustomFunctionCPython && ) = delete;
    CustomFunctionCPython &operator=( CustomFunctionCPython && ) = delete;

    CustomFunctionInvokeResult invoke( CustomFunctionInvocationID invocationId,
                                       const std::vector<InspectionValue> &args );
    void cleanup( CustomFunctionInvocationID invocationId );

private:
    struct InvocationState
    {
        std::string modName;
        void *mod{};
    };
    std::shared_ptr<CustomFunctionScriptEngine> mScriptEngine;
    std::unordered_map<CustomFunctionInvocationID, InvocationState> mInvocationStates;
    void *mThreadState{};

    CustomFunctionInvokeResult invokeScript( CustomFunctionInvocationID invocationId,
                                             const std::vector<InspectionValue> &args );

    static ExpressionErrorCode addToSysPath( const std::string &directory );
};

} // namespace IoTFleetWise
} // namespace Aws
