// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LogLevel.h"
#include <cstdint>
#include <functional>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

class ILogger
{
public:
    virtual ~ILogger() = default;

    /**
     * @brief Logs a log message in different way depending on the implementation class
     *
     * @param level log level
     * @param filename calling file
     * @param lineNumber line number
     * @param function calling function
     * @param logEntry actual message
     */
    virtual void logMessage( LogLevel level,
                             const std::string &filename,
                             const uint32_t lineNumber,
                             const std::string &function,
                             const std::string &logEntry ) = 0;

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

} // namespace IoTFleetWise
} // namespace Aws
