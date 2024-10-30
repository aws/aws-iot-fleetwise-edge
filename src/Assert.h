// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LoggingModule.h"
#include <csignal>
#include <cstdlib>
#include <string>

#ifdef __COVERITY__
#include <cassert>
#endif

// coverity[autosar_cpp14_m16_3_2_violation] '#' operator required for debug text only
// coverity[misra_cpp_2008_rule_16_3_2_violation] '#' operator required for debug text only
#define FWE_FATAL_ASSERT( cond, msg )                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( cond ) )                                                                                               \
        {                                                                                                              \
            FWE_LOG_ERROR( "Fatal error condition occurred, aborting application: " + std::string( msg ) + " " +       \
                           std::string( #cond ) );                                                                     \
            LoggingModule::flush();                                                                                    \
            fatalError();                                                                                              \
        }                                                                                                              \
    } while ( false )

// coverity[autosar_cpp14_m16_3_2_violation] '#' operator required for debug text only
// coverity[misra_cpp_2008_rule_16_3_2_violation] '#' operator required for debug text only
#define FWE_GRACEFUL_FATAL_ASSERT( cond, msg, returnValue )                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( cond ) )                                                                                               \
        {                                                                                                              \
            FWE_LOG_ERROR( "Fatal error condition occurred, exiting application: " + std::string( msg ) + " " +        \
                           std::string( #cond ) );                                                                     \
            LoggingModule::flush();                                                                                    \
            std::raise( SIGUSR1 );                                                                                     \
            return returnValue;                                                                                        \
        }                                                                                                              \
    } while ( false )

namespace Aws
{
namespace IoTFleetWise
{

inline void
fatalError()
{
#ifdef __COVERITY__
    // coverity[misra_cpp_2008_rule_5_2_12_violation] Error from cassert header
    // coverity[autosar_cpp14_m5_2_12_violation] Error from cassert header
    assert( false );
#else
    abort();
#endif
}

} // namespace IoTFleetWise
} // namespace Aws
