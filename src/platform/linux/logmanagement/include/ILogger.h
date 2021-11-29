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
#include "LogLevel.h"
#include <functional>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{

class ILogger
{
public:
    virtual ~ILogger() = 0;
    /**
     * @brief Logs a log message in different way depending on the implementation class
     *
     * @param level log level
     * @param function calling function
     * @param logEntry actual message
     */
    virtual void logMessage( LogLevel level, const std::string &function, const std::string &logEntry ) = 0;

    /**
     * @brief converts the Log Level enum to a human readable string
     * @param level the log level enum to convert
     * @return empty string if unrecognized LogLevel
     * */
    static const std::string &levelToString( LogLevel level );
};
/**
 * @brief That the single instance of ILogger that should be used for forwarding Logs besides ConsoleLogger
 *
 * Can be called safe from any thread
 *
 * @param logForwarder the instance that should be receive Logs
 */
extern void setLogForwarding( ILogger *logForwarder );

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws