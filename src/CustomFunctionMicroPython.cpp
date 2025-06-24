// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CustomFunctionMicroPython.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <utility>

extern "C"
{
// coverity[misra_cpp_2008_rule_16_0_1_violation] extern C required before include
// coverity[autosar_cpp14_m16_0_1_violation] extern C required before include
#include <py/mpconfig.h> // IWYU pragma: keep
// Deliberate gap to force include of <py/mpconfig.h> before <py/misc.h>
#include <port/micropython_embed.h>
#include <py/misc.h>
#include <py/mpprint.h>
#include <py/mpstate.h>
#include <py/nlr.h>
#include <py/obj.h>
#include <py/qstr.h>
#include <py/runtime.h>
}

#ifdef __COVERITY__
// coverity[misra_cpp_2008_rule_16_0_3_violation]
#undef mp_obj_is_bool
#define mp_obj_is_bool( o ) ( true )
// coverity[misra_cpp_2008_rule_16_0_3_violation]
#undef mp_obj_is_float
#define mp_obj_is_float( o ) ( true )
// coverity[misra_cpp_2008_rule_16_0_3_violation]
#undef mp_const_none
#define mp_const_none reinterpret_cast<mp_obj_t>( 0 )
// coverity[misra_cpp_2008_rule_16_0_3_violation]
#undef MP_OBJ_NEW_SMALL_INT
#define MP_OBJ_NEW_SMALL_INT( small_int ) reinterpret_cast<mp_obj_t>( 0 )
// coverity[misra_cpp_2008_rule_16_0_3_violation]
#undef MP_OBJ_NEW_QSTR
#define MP_OBJ_NEW_QSTR( qst ) reinterpret_cast<mp_obj_t>( 0 )
#endif

namespace Aws
{
namespace IoTFleetWise
{

void
CustomFunctionMicroPython::printError( const char *str, size_t len )
{
    mErrorString += std::string( str, len );
    if ( ( !mErrorString.empty() ) && ( mErrorString[mErrorString.size() - 1] == '\n' ) )
    {
        mErrorString.pop_back();
        FWE_LOG_ERROR( mErrorString );
        mErrorString.clear();
    }
}

void
CustomFunctionMicroPython::printException( void *e )
{
    mErrorString.clear();
    // coverity[misra_cpp_2008_rule_0_1_5_violation] False positive, it's used on the next line
    // coverity[misra_cpp_2008_rule_8_5_2_violation] False positive, braces are used
    // coverity[autosar_cpp14_a0_1_6_violation] False positive, it's used on the next line
    // coverity[autosar_cpp14_a8_4_10_violation] raw pointer needed to match the expected signature
    // coverity[autosar_cpp14_m8_5_2_violation] False positive, braces are used
    mp_print_t printConfig{ this, []( void *env, const char *str, size_t len ) {
                               auto me = reinterpret_cast<CustomFunctionMicroPython *>( env );
                               me->printError( str, len );
                           } };
    mp_obj_print_exception( &printConfig, e );
}

CustomFunctionMicroPython::CustomFunctionMicroPython( std::shared_ptr<CustomFunctionScriptEngine> scriptEngine )
    : mScriptEngine( std::move( scriptEngine ) )
{
}

CustomFunctionMicroPython::~CustomFunctionMicroPython()
{
    if ( mInitialized )
    {
        // Cleanup all remaining invocations:
        while ( !mInvocationStates.empty() )
        {
            cleanup( mInvocationStates.begin()->first );
        }
        mp_embed_deinit();
    }
}

CustomFunctionInvokeResult
CustomFunctionMicroPython::invoke( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    if ( !mInitialized )
    {
        mInitialized = true;
        // Need to initialize on the CIE thread to get the right stack top value
        int stackTop{};
        mp_embed_init( &mHeap[0], sizeof( mHeap ), &stackTop );
        auto downloadDirectory = mScriptEngine->getDownloadDirectory();
        nlr_buf_t nlr;
        if ( nlr_push( &nlr ) == 0 ) // MicroPython: try
        {
            mp_sys_path = mp_obj_new_list( 0, nullptr );
            // Add download directory to sys.path:
            mp_obj_list_append( mp_sys_path, mp_obj_new_str( downloadDirectory.c_str(), downloadDirectory.size() ) );
            nlr_pop(); // MicroPython: Always needed at the end of the try-block
        }
        else // MicroPython: catch
        {
            printException( nlr.ret_val );
        }
    }

    auto scriptStatus = mScriptEngine->setup( invocationId, args );
    if ( scriptStatus == CustomFunctionScriptEngine::ScriptStatus::ERROR )
    {
        return ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( scriptStatus == CustomFunctionScriptEngine::ScriptStatus::DOWNLOADING )
    {
        return ExpressionErrorCode::SUCCESSFUL; // Undefined value
    }

    // Create C++ objects that can use heap-allocation outside of MicroPython try/catch block, otherwise they could leak
    // when an exception occurs
    std::string scriptDirectory;
    CustomFunctionInvokeResult result{ ExpressionErrorCode::TYPE_MISMATCH };
    nlr_buf_t nlr;
    if ( nlr_push( &nlr ) == 0 ) // MicroPython: try
    {
        auto stateIt = mInvocationStates.find( invocationId );
        if ( stateIt == mInvocationStates.end() )
        {
            stateIt = mInvocationStates.emplace( invocationId, InvocationState{} ).first;
            if ( ( args.size() < 2 ) || ( !args[1].isString() ) )
            {
                qstr invocationIdHexQstr{};
                {
                    // Convert to Qstr to avoid leaking std::string when exception is raised
                    auto invocationIdHex = customFunctionInvocationIdToHexString( invocationId );
                    invocationIdHexQstr = qstr_from_strn( invocationIdHex.c_str(), invocationIdHex.size() );
                }
                mp_raise_msg_varg( &mp_type_RuntimeError,
                                   MP_ERROR_TEXT( "Module to invoke not provided or not string for invocation ID %q" ),
                                   invocationIdHexQstr );
            }
            stateIt->second.modName = mScriptEngine->getScriptName( invocationId ) + "." + *args[1].stringVal;
            // Add script directory to sys.path:
            scriptDirectory = mScriptEngine->getScriptDirectory( invocationId );
            mp_obj_list_append( mp_sys_path, mp_obj_new_str( scriptDirectory.c_str(), scriptDirectory.size() ) );
            // Import the module:
            auto fromList = mp_obj_new_list( 0, nullptr );
            mp_obj_list_append( fromList, MP_OBJ_NEW_QSTR( MP_QSTR_invoke ) ); // NOLINT
            stateIt->second.mod =
                mp_import_name( qstr_from_strn( stateIt->second.modName.c_str(), stateIt->second.modName.size() ),
                                fromList,
                                MP_OBJ_NEW_SMALL_INT( 0 ) ); // NOLINT
        }

        // Positional params to the function are the remaining args:
        auto params = mp_obj_new_list( 0, nullptr );
        for ( size_t i = 2; i < args.size(); i++ )
        {
            const auto &arg = args[i];
            switch ( arg.type )
            {
            case InspectionValue::DataType::UNDEFINED:
                mp_obj_list_append( params, mp_const_none ); // NOLINT
                break;
            case InspectionValue::DataType::BOOL:
                mp_obj_list_append( params, mp_obj_new_bool( static_cast<int>( arg.boolVal ) ) );
                break;
            case InspectionValue::DataType::DOUBLE:
                mp_obj_list_append( params, mp_obj_new_float( arg.doubleVal ) );
                break;
            case InspectionValue::DataType::STRING:
                mp_obj_list_append( params, mp_obj_new_str( arg.stringVal->c_str(), arg.stringVal->size() ) );
                break;
            }
        }

        // Find the 'invoke' function in the module:
        auto invokeFunction = mp_load_attr( stateIt->second.mod, MP_QSTR_invoke );
        // Call the function:
        size_t paramsLen{};
        mp_obj_t *paramsItems{};
        mp_obj_list_get( params, &paramsLen, &paramsItems );
        auto res = mp_call_function_n_kw( invokeFunction, paramsLen, 0, paramsItems );

        // If a tuple is returned, the first value is the result and the second value is collected data
        if ( mp_obj_get_type( res ) == &mp_type_tuple )
        {
            size_t tupleLen{};
            mp_obj_t *tupleItems{};
            mp_obj_tuple_get( res, &tupleLen, &tupleItems );
            if ( tupleLen < 2 )
            {
                mp_raise_msg( &mp_type_RuntimeError, MP_ERROR_TEXT( "Unexpected tuple size" ) );
            }
            res = tupleItems[0];
            auto collectedDataDict = tupleItems[1];
            if ( !mp_obj_is_dict_or_ordereddict( collectedDataDict ) )
            {
                mp_raise_msg( &mp_type_RuntimeError, MP_ERROR_TEXT( "Collected data is not a dict" ) );
            }
            auto collectedDataMap = mp_obj_dict_get_map( collectedDataDict );
            for ( size_t i = 0; i < collectedDataMap->used; i++ )
            {
                const auto &element = collectedDataMap->table[i];
                mScriptEngine->mCollectedData.emplace( mp_obj_str_get_str( element.key ),
                                                       mp_obj_str_get_str( element.value ) );
            }
        }

        // coverity[misra_cpp_2008_rule_5_2_1_violation] Error from library header macro
        if ( mp_obj_is_bool( res ) ) // NOLINT
        {
            result = { ExpressionErrorCode::SUCCESSFUL, mp_obj_is_true( res ) };
        }
        // coverity[misra_cpp_2008_rule_5_0_13_violation] Error from library header macro
        // coverity[misra_cpp_2008_rule_5_2_12_violation] Error from library header macro
        // coverity[misra_cpp_2008_rule_5_3_1_violation] Error from library header macro
        // coverity[misra_cpp_2008_rule_5_18_1_violation] Error from library header macro
        // coverity[autosar_cpp14_a5_0_2_violation] Error from library header macro
        // coverity[autosar_cpp14_m5_2_12_violation] Error from library header macro
        // coverity[autosar_cpp14_m5_3_1_violation] Error from library header macro
        // coverity[autosar_cpp14_m5_18_1_violation] Error from library header macro
        else if ( mp_obj_is_float( res ) ) // NOLINT
        {
            result = { ExpressionErrorCode::SUCCESSFUL, mp_obj_get_float( res ) };
        }
        else if ( mp_obj_is_int( res ) ) // NOLINT
        {
            result = { ExpressionErrorCode::SUCCESSFUL, static_cast<int>( mp_obj_get_int( res ) ) };
        }
        else if ( mp_obj_is_str( res ) ) // NOLINT
        {
            result = { ExpressionErrorCode::SUCCESSFUL, mp_obj_str_get_str( res ) };
        }
        else // Including None
        {
            result = ExpressionErrorCode::SUCCESSFUL;
        }

        nlr_pop(); // MicroPython: Always needed at the end of the try-block
    }
    else // MicroPython: catch
    {
        mScriptEngine->setStatus( invocationId, CustomFunctionScriptEngine::ScriptStatus::ERROR );
        printException( nlr.ret_val );
    }
    return result;
}

void
CustomFunctionMicroPython::cleanup( CustomFunctionInvocationID invocationId )
{
    mScriptEngine->cleanup( invocationId );
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() )
    {
        return;
    }

    if ( stateIt->second.mod != nullptr )
    {
        // Call the module's cleanup method if it exists:
        nlr_buf_t nlr;
        if ( nlr_push( &nlr ) == 0 ) // MicroPython: try
        {
            mp_obj_t dest[2]{};
            mp_load_method_maybe( stateIt->second.mod, MP_QSTR_cleanup, &dest[0] );
            if ( dest[0] != MP_OBJ_NULL )
            {
                mp_call_function_0( dest[0] );
            }
            nlr_pop(); // MicroPython: Always needed at the end of the try-block
        }
        else // MicroPython: catch
        {
            printException( nlr.ret_val );
        }

        if ( nlr_push( &nlr ) == 0 ) // MicroPython: try
        {
            // Unreference the module so it can be garbage collected:
            auto loadedModulesMap = &MP_STATE_VM( mp_loaded_modules_dict ).map;
            mp_map_lookup( loadedModulesMap,
                           mp_obj_new_str( stateIt->second.modName.c_str(), // NOLINT
                                           stateIt->second.modName.size() ),
                           MP_MAP_LOOKUP_REMOVE_IF_FOUND );

            nlr_pop(); // MicroPython: Always needed at the end of the try-block
        }
        else // MicroPython: catch
        {
            printException( nlr.ret_val );
        }
    }
    mInvocationStates.erase( stateIt );
}

} // namespace IoTFleetWise
} // namespace Aws
