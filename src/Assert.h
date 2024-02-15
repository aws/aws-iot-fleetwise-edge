// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LoggingModule.h"
#include <csignal>
#include <cstdlib>
#include <string>

#define FWE_FATAL_ASSERT( cond, msg )                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if ( !( cond ) )                                                                                               \
        {                                                                                                              \
            FWE_LOG_ERROR( "Fatal error condition occurred, aborting application: " + std::string( msg ) + " " +       \
                           std::string( #cond ) );                                                                     \
            LoggingModule::flush();                                                                                    \
            abort();                                                                                                   \
        }                                                                                                              \
    } while ( 0 )

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
    } while ( 0 )
