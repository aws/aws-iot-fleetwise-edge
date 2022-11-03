// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes

#include "ConsoleLogger.h"
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
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
    /**
     * @brief Logs bytes in a vector
     * @param function Calling function
     * @param logEntry actual messages
     * @param inputBytes bytes in vector to be printed
     */
    void traceBytesInVector( const std::string &function,
                             const std::string &logEntry,
                             const std::vector<uint8_t> &inputBytes );

private:
    ConsoleLogger mLogger;
};
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws