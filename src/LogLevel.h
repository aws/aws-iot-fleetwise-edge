// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Log levels.
 */
enum class LogLevel
{
    Trace,
    Info,
    Warning,
    Error,
    Off
};

inline bool
stringToLogLevel( const std::string level, LogLevel &outLogLevel )
{
    if ( level == "Info" )
    {
        outLogLevel = LogLevel::Info;
    }
    else if ( level == "Error" )
    {
        outLogLevel = LogLevel::Error;
    }
    else if ( level == "Warning" )
    {
        outLogLevel = LogLevel::Warning;
    }
    else if ( level == "Trace" )
    {
        outLogLevel = LogLevel::Trace;
    }
    else if ( level == "Off" )
    {
        outLogLevel = LogLevel::Off;
    }
    else
    {
        return false;
    }
    return true;
}

extern LogLevel gSystemWideLogLevel;

} // namespace IoTFleetWise
} // namespace Aws
