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

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "ILogger.h"
#include "LogLevel.h"
#include <functional>
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
 * @brief This logger instance logs messages to the standard output.
 */
class ConsoleLogger : public ILogger
{
public:
    ~ConsoleLogger() override = default;
    ConsoleLogger() = default;
    ConsoleLogger( const ConsoleLogger & ) = delete;
    ConsoleLogger &operator=( const ConsoleLogger & ) = delete;
    ConsoleLogger( ConsoleLogger && ) = delete;
    ConsoleLogger &operator=( ConsoleLogger && ) = delete;

    /**
     * @brief Logs a log message to the standard output. Includes current Thread ID and timestamp.
     *        The log message has this structure : [Thread : ID] [Time] [Level] [function]: [Message]
     * @param level log level
     * @param function calling function
     * @param logEntry actual message
     */
    void logMessage( LogLevel level, const std::string &function, const std::string &logEntry ) override;

private:
    /**
     * @brief Time stamp as a String. The actual timestamp conversion and format happens
     *        in the Clock API
     * @return the current time in a string representation as it's defined in the clock.
     */
    static std::string timeAsString();

    /**
     * @brief Current Thread ID that's logging the message.
     * @return Thread ID.
     */
    static uint64_t currentThreadId();
};
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX