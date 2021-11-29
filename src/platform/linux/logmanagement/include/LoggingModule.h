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

#include "ConsoleLogger.h"
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
/**
 * @brief Logging API. Used by all modules of the software to forward log entries
 * to a console logger instance.
 */
class LoggingModule
{
public:
    static const uint64_t LOG_AGGREGATION_TIME_MS =
        1000; /*< Periodic logging for one module should have a maximum frequency of this */

    LoggingModule();

    /**
     * @brief Logs an Error
     * @param function Calling function
     * @param logEntry actual messages
     */
    void error( const std::string &function, const std::string &logEntry );
    /**
     * @brief Logs a Warning
     * @param function Calling function
     * @param logEntry actual messages
     */
    void warn( const std::string &function, const std::string &logEntry );
    /**
     * @brief Logs an Info
     * @param function Calling function
     * @param logEntry actual messages
     */
    void info( const std::string &function, const std::string &logEntry );
    /**
     * @brief Logs a Trace
     * @param function Calling function
     * @param logEntry actual messages
     */
    void trace( const std::string &function, const std::string &logEntry );

private:
    ConsoleLogger mLogger;
};

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws