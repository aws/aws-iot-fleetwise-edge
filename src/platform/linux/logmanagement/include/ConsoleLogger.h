// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "ILogger.h"
#include "LogLevel.h"
#include <functional>
#include <string>

namespace Color
{
static const std::string red{ "\x1b[31m" };
static const std::string yellow{ "\x1b[33m" };
static const std::string blue{ "\x1b[34m" };
static const std::string normal;
static const std::string reset{ "\x1b[0m" };
} // namespace Color

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
    ConsoleLogger();
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

    /**
     * @brief converts the Log Level enum to a color for logging
     * @param level the log level enum to convert
     * @return empty string if unrecognized LogLevel
     * */
    const std::string &levelToColor( LogLevel level ) const;

    bool mColorEnabled;
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
        outLogColorOption = Aws::IoTFleetWise::Platform::Linux::LogColorOption::Auto;
    }
    else if ( level == "Yes" )
    {
        outLogColorOption = Aws::IoTFleetWise::Platform::Linux::LogColorOption::Yes;
    }
    else if ( level == "No" )
    {
        outLogColorOption = Aws::IoTFleetWise::Platform::Linux::LogColorOption::No;
    }
    else
    {
        return false;
    }
    return true;
}

extern LogColorOption gLogColorOption;
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX