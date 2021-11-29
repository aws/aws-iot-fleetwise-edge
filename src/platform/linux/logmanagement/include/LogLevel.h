/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#pragma once

// Includes
#include <string>
namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
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
        outLogLevel = Aws::IoTFleetWise::Platform::LogLevel::Info;
    }
    else if ( level == "Error" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::LogLevel::Error;
    }
    else if ( level == "Warning" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::LogLevel::Warning;
    }
    else if ( level == "Trace" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::LogLevel::Trace;
    }
    else if ( level == "Off" )
    {
        outLogLevel = Aws::IoTFleetWise::Platform::LogLevel::Off;
    }
    else
    {
        return false;
    }
    return true;
}

extern LogLevel gSystemWideLogLevel;
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws