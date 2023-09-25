// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ILogger.h"
#include "LogLevel.h"
#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This logger instance logs messages to the standard output.
 */

class ConsoleLogger : public ILogger
{
public:
    ~ConsoleLogger() override = default;
    ConsoleLogger();
    ConsoleLogger( const ConsoleLogger & ) = delete;
    ConsoleLogger &operator=( const ConsoleLogger & ) = delete;
    ConsoleLogger( ConsoleLogger && ) = delete;
    ConsoleLogger &operator=( ConsoleLogger && ) = delete;

    /**
     * @brief Logs a log message to the standard output. Includes current Thread ID and timestamp.
     *        The log message has this structure : [Thread : ID] [Time] [Level] [filename:lineNumber] [function]:
     * [Message]
     * @param level log level
     * @param filename file that emitted the log
     * @param lineNumber line number
     * @param function calling function
     * @param logEntry actual message
     */
    void logMessage( LogLevel level,
                     const std::string &filename,
                     const uint32_t lineNumber,
                     const std::string &function,
                     const std::string &logEntry ) override;

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

    /**
     * @brief converts the Log Level enum to a color for logging
     * @param level the log level enum to convert
     * @return empty string if unrecognized LogLevel
     * */
    const std::string &levelToColor( LogLevel level ) const;

    bool mColorEnabled{ false };
};

enum class LogColorOption
{
    Auto,
    Yes,
    No
};

inline bool
stringToLogColorOption( const std::string level, LogColorOption &outLogColorOption )
{
    if ( level == "Auto" )
    {
        outLogColorOption = LogColorOption::Auto;
    }
    else if ( level == "Yes" )
    {
        outLogColorOption = LogColorOption::Yes;
    }
    else if ( level == "No" )
    {
        outLogColorOption = LogColorOption::No;
    }
    else
    {
        return false;
    }
    return true;
}

extern LogColorOption gLogColorOption;

} // namespace IoTFleetWise
} // namespace Aws
