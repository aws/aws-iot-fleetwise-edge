// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CustomFunctionCPython.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <Python.h>
#include <cstdlib>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

using PyObjectUniquePtr = std::unique_ptr<PyObject, decltype( &_Py_XDECREF )>;

CustomFunctionCPython::CustomFunctionCPython( std::shared_ptr<CustomFunctionScriptEngine> scriptEngine )
    : mScriptEngine( std::move( scriptEngine ) )
{
    Py_Initialize();
    addToSysPath( mScriptEngine->getDownloadDirectory() );
    mThreadState = PyEval_SaveThread();
}

CustomFunctionCPython::~CustomFunctionCPython()
{
    PyEval_RestoreThread( reinterpret_cast<PyThreadState *>( mThreadState ) );
    // Cleanup all remaining invocations:
    while ( !mInvocationStates.empty() )
    {
        cleanup( mInvocationStates.begin()->first );
    }
    Py_Finalize();
}

ExpressionErrorCode
CustomFunctionCPython::addToSysPath( const std::string &directory )
{
    PyObjectUniquePtr directoryObj(
        PyUnicode_DecodeUTF8( directory.c_str(), static_cast<Py_ssize_t>( directory.size() ), "replace" ),
        _Py_XDECREF );
    if ( directoryObj.get() == nullptr )
    {
        FWE_LOG_ERROR( "Error converting script directory to object" );
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    auto pathList = PySys_GetObject( "path" );
    if ( pathList == nullptr )
    {
        FWE_LOG_ERROR( "Couldn't get sys.path" );
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( PyList_Append( pathList, directoryObj.get() ) != 0 )
    {
        FWE_LOG_ERROR( "Error appending to sys.path" );
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    return ExpressionErrorCode::SUCCESSFUL;
}

CustomFunctionInvokeResult
CustomFunctionCPython::invokeScript( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() )
    {
        stateIt = mInvocationStates.emplace( invocationId, InvocationState{} ).first;
        if ( ( args.size() < 2 ) || ( !args[1].isString() ) )
        {
            FWE_LOG_ERROR( "Module to invoke not provided or not string for invocation ID " +
                           customFunctionInvocationIdToHexString( invocationId ) );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        stateIt->second.modName = mScriptEngine->getScriptName( invocationId ) + "." + *args[1].stringVal;
        auto res = addToSysPath( mScriptEngine->getScriptDirectory( invocationId ) );
        if ( res != ExpressionErrorCode::SUCCESSFUL )
        {
            return res;
        }
        // Import the module:
        stateIt->second.mod = PyImport_ImportModule( stateIt->second.modName.c_str() );
        if ( stateIt->second.mod == nullptr )
        {
            FWE_LOG_ERROR( "Error importing module " + stateIt->second.modName );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
    }

    // Positional params to the function are the remaining args:
    PyObjectUniquePtr params( PyTuple_New( static_cast<Py_ssize_t>( args.size() ) - 2 ), _Py_XDECREF );
    if ( params.get() == nullptr )
    {
        FWE_LOG_ERROR( "Error creating tuple" );
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    for ( size_t i = 2; i < args.size(); i++ )
    {
        const auto &arg = args[i];
        PyObject *param{};
        switch ( arg.type )
        {
        case InspectionValue::DataType::UNDEFINED:
            param = Py_None;
            Py_INCREF( param );
            break;
        case InspectionValue::DataType::BOOL:
            param = PyBool_FromLong( arg.boolVal ? 1 : 0 );
            break;
        case InspectionValue::DataType::DOUBLE:
            param = PyFloat_FromDouble( arg.doubleVal );
            if ( param == nullptr )
            {
                FWE_LOG_ERROR( "Error converting double value" );
                return ExpressionErrorCode::TYPE_MISMATCH;
            }
            break;
        case InspectionValue::DataType::STRING:
            param = PyUnicode_DecodeUTF8(
                arg.stringVal->data(), static_cast<Py_ssize_t>( arg.stringVal->size() ), "replace" );
            if ( param == nullptr )
            {
                FWE_LOG_ERROR( "Error converting string value" );
                return ExpressionErrorCode::TYPE_MISMATCH;
            }
            break;
        }
        // steals reference to param:
        if ( PyTuple_SetItem( params.get(), static_cast<Py_ssize_t>( i ) - 2, param ) != 0 )
        {
            FWE_LOG_ERROR( "Error setting tuple value" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
    }

    // Find the 'invoke' function in the module:
    PyObjectUniquePtr invokeFunction(
        PyObject_GetAttrString( reinterpret_cast<PyObject *>( stateIt->second.mod ), "invoke" ), _Py_XDECREF );
    if ( invokeFunction.get() == nullptr )
    {
        FWE_LOG_ERROR( "Could not find invoke method in module " + stateIt->second.modName );
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    // Call the function:
    PyObjectUniquePtr res( PyObject_CallObject( invokeFunction.get(), params.get() ), _Py_XDECREF );
    if ( res.get() == nullptr )
    {
        FWE_LOG_ERROR( "Error while calling invoke method" );
        return ExpressionErrorCode::TYPE_MISMATCH;
    }

    // If a tuple is returned, the first value is the result and the second value is collected data
    PyObject *resVal{};
    // coverity[misra_cpp_2008_rule_5_0_10_violation] Error from library header macro
    // coverity[autosar_cpp14_m5_0_10_violation] Error from library header macro
    if ( PyTuple_Check( res.get() ) == 0 )
    {
        resVal = res.get();
    }
    else
    {
        auto tupleLen = PyTuple_Size( res.get() );
        if ( tupleLen < 2 )
        {
            FWE_LOG_ERROR( "Unexpected tuple size" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        auto collectedDataDict = PyTuple_GetItem( res.get(), 1 );
        resVal = PyTuple_GetItem( res.get(), 0 ); // returns borrowed reference, no need to DECREF
        // coverity[misra_cpp_2008_rule_5_0_10_violation] Error from library header macro
        // coverity[autosar_cpp14_m5_0_10_violation] Error from library header macro
        if ( PyDict_Check( collectedDataDict ) == 0 )
        {
            FWE_LOG_ERROR( "Collected data is not a dict" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        Py_ssize_t pos{};
        PyObject *key{};
        PyObject *value{};
        while ( PyDict_Next( collectedDataDict, &pos, &key, &value ) != 0 )
        {
            auto keyStr = PyUnicode_AsUTF8( key );
            if ( keyStr == nullptr )
            {
                FWE_LOG_ERROR( "Error converting key to string" );
                return ExpressionErrorCode::TYPE_MISMATCH;
            }
            auto valueStr = PyUnicode_AsUTF8( value );
            if ( valueStr == nullptr )
            {
                FWE_LOG_ERROR( "Error converting value to string" );
                return ExpressionErrorCode::TYPE_MISMATCH;
            }
            mScriptEngine->mCollectedData.emplace( keyStr, valueStr );
        }
    }

    if ( PyBool_Check( resVal ) != 0 )
    {
        auto value = PyObject_IsTrue( resVal );
        if ( value < 0 )
        {
            FWE_LOG_ERROR( "Error converting to bool" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        return { ExpressionErrorCode::SUCCESSFUL, value != 0 };
    }
    // coverity[misra_cpp_2008_rule_5_0_13_violation] Error from library header macro
    // coverity[misra_cpp_2008_rule_5_2_1_violation] Error from library header macro
    // coverity[misra_cpp_2008_rule_5_3_1_violation] Error from library header macro
    // coverity[autosar_cpp14_a5_0_2_violation] Error from library header macro
    // coverity[autosar_cpp14_m5_3_1_violation] Error from library header macro
    else if ( PyFloat_Check( resVal ) != 0 )
    {
        auto value = PyFloat_AsDouble( resVal );
        if ( PyErr_Occurred() != nullptr )
        {
            FWE_LOG_ERROR( "Error converting to double" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        return { ExpressionErrorCode::SUCCESSFUL, value };
    }
    // coverity[misra_cpp_2008_rule_5_0_10_violation] Error from library header macro
    // coverity[autosar_cpp14_m5_0_10_violation] Error from library header macro
    else if ( PyLong_Check( resVal ) != 0 )
    {
        auto value = PyLong_AsLong( resVal );
        if ( PyErr_Occurred() != nullptr )
        {
            FWE_LOG_ERROR( "Error converting to integer" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        return { ExpressionErrorCode::SUCCESSFUL, static_cast<int>( value ) };
    }
    // coverity[misra_cpp_2008_rule_5_0_10_violation] Error from library header macro
    // coverity[autosar_cpp14_m5_0_10_violation] Error from library header macro
    else if ( PyUnicode_Check( resVal ) != 0 )
    {
        auto value = PyUnicode_AsUTF8( resVal );
        if ( value == nullptr )
        {
            FWE_LOG_ERROR( "Error converting value to string" );
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        return { ExpressionErrorCode::SUCCESSFUL, value };
    }
    else // Including None
    {
        return ExpressionErrorCode::SUCCESSFUL;
    }
}

CustomFunctionInvokeResult
CustomFunctionCPython::invoke( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    auto scriptStatus = mScriptEngine->setup( invocationId, args );
    if ( scriptStatus == CustomFunctionScriptEngine::ScriptStatus::ERROR )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( scriptStatus == CustomFunctionScriptEngine::ScriptStatus::DOWNLOADING )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined value
    }
    auto gilState = PyGILState_Ensure();
    auto result = invokeScript( invocationId, args );
    if ( result.error != ExpressionErrorCode::SUCCESSFUL )
    {
        mScriptEngine->setStatus( invocationId, CustomFunctionScriptEngine::ScriptStatus::ERROR );
        PyErr_Print();
    }
    PyGILState_Release( gilState );
    return result;
}

void
CustomFunctionCPython::cleanup( CustomFunctionInvocationID invocationId )
{
    mScriptEngine->cleanup( invocationId );
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() )
    {
        return;
    }

    if ( stateIt->second.mod != nullptr )
    {
        auto gilState = PyGILState_Ensure();
        // Call the module's cleanup method if it exists:
        PyObjectUniquePtr cleanupFunction(
            PyObject_GetAttrString( reinterpret_cast<PyObject *>( stateIt->second.mod ), "cleanup" ), _Py_XDECREF );
        if ( cleanupFunction.get() == nullptr )
        {
            PyErr_Clear();
        }
        else
        {
            // Call the function:
            PyObjectUniquePtr res( PyObject_CallObject( cleanupFunction.get(), nullptr ), _Py_XDECREF );
            if ( res.get() == nullptr )
            {
                FWE_LOG_ERROR( "Error while calling cleanup method" );
                PyErr_Print();
            }
        }

        // It's not possible to unload a module in CPython, but let's at least DECREF our reference:
        Py_DECREF( reinterpret_cast<PyObject *>( stateIt->second.mod ) );
        PyGILState_Release( gilState );
    }
    mInvocationStates.erase( stateIt );
}

} // namespace IoTFleetWise
} // namespace Aws
