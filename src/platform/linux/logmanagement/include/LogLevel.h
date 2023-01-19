// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Includes
#include <string>
namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
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
        outLogLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Info;
    }
    else if ( level == "Error" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Error;
    }
    else if ( level == "Warning" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Warning;
    }
    else if ( level == "Trace" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Trace;
    }
    else if ( level == "Off" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Off;
    }
    else
    {
        return false;
    }
    return true;
}

extern LogLevel gSystemWideLogLevel;
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
