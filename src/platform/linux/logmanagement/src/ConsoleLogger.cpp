// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "ConsoleLogger.h"
#include "ClockHandler.h"
#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <chrono>
#include <cinttypes>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{
namespace Color
{
static const std::string red{ "\x1b[31m" };
static const std::string yellow{ "\x1b[33m" };
static const std::string blue{ "\x1b[34m" };
static const std::string normal;
static const std::string reset{ "\x1b[0m" };
} // namespace Color

LogLevel gSystemWideLogLevel;
LogColorOption gLogColorOption = LogColorOption::Auto;

static std::mutex gLogForwardingMutex;
static ILogger *gLogForwarder = nullptr;

void
setLogForwarding( ILogger *logForwarder )
{
    std::lock_guard<std::mutex> lock( gLogForwardingMutex );
    gLogForwarder = logForwarder;
}

static void
forwardLog( LogLevel level,
            const std::string &filename,
            const uint32_t lineNumber,
            const std::string &function,
            const std::string &logEntry )
{
    if ( gLogForwarder == nullptr )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( gLogForwardingMutex );
    if ( gLogForwarder == nullptr )
    {
        return;
    }
    else
    {
        gLogForwarder->logMessage( level, filename, lineNumber, function, logEntry );
    }
}

ConsoleLogger::ConsoleLogger()
{
    if ( ( gLogColorOption == LogColorOption::Yes ) ||
         // Connected to the terminal
         ( ( gLogColorOption == LogColorOption::Auto ) && ( isatty( fileno( stdout ) ) != 0 ) ) )
    {
        mColorEnabled = true;
    }
}

#ifdef __ANDROID__
static android_LogPriority
levelToAndroidLevel( LogLevel level )
{
    switch ( level )
    {
    case LogLevel::Error:
        return ANDROID_LOG_ERROR;
    case LogLevel::Warning:
        return ANDROID_LOG_WARN;
    case LogLevel::Trace:
        return ANDROID_LOG_DEBUG;
    case LogLevel::Info:
    default:
        return ANDROID_LOG_INFO;
    }
}
#endif

void
ConsoleLogger::logMessage( LogLevel level,
                           const std::string &filename,
                           const uint32_t lineNumber,
                           const std::string &function,
                           const std::string &logEntry )
{
    if ( level >= gSystemWideLogLevel )
    {
#ifdef __ANDROID__
        __android_log_print( levelToAndroidLevel( level ),
                             "FWE",
                             "[Thread: %" PRIu64 "] [%s] [%s:%i] [%s()]: [%s]",
                             currentThreadId(),
                             timeAsString().c_str(),
                             filename.c_str(),
                             lineNumber,
                             function.c_str(),
                             logEntry.c_str() );
#else
        std::printf( "%s[Thread: %" PRIu64 "] [%s] [%s] [%s:%i] [%s()]: [%s]%s\n",
                     levelToColor( level ).c_str(),
                     currentThreadId(),
                     timeAsString().c_str(),
                     levelToString( level ).c_str(),
                     filename.c_str(),
                     lineNumber,
                     function.c_str(),
                     logEntry.c_str(),
                     mColorEnabled ? Color::reset.c_str() : "" );
#endif
        forwardLog( level, filename, lineNumber, function, logEntry );
    }
}

std::string
ConsoleLogger::timeAsString()
{
    auto clock = ClockHandler::getClock();
    return clock->currentTimeToIsoString();
}

const std::string &
ConsoleLogger::levelToColor( LogLevel level ) const
{
    if ( !mColorEnabled )
    {
        return Color::normal;
    }

    switch ( level )
    {
    case LogLevel::Error:
        return Color::red;
    case LogLevel::Warning:
        return Color::yellow;
    case LogLevel::Trace:
        return Color::blue;
    case LogLevel::Info:
    default:
        return Color::normal;
    }
}

const std::string &
ILogger::levelToString( LogLevel level )
{
    static const std::string error( "ERROR" );
    static const std::string warn( "WARN " ); // Note: extra space to align the log columns
    static const std::string info( "INFO " ); // Note: extra space to align the log columns
    static const std::string trace( "TRACE" );
    static const std::string none;

    switch ( level )
    {
    case LogLevel::Error:
        return error;
        break;
    case LogLevel::Warning:
        return warn;
        break;
    case LogLevel::Info:
        return info;
        break;
    case LogLevel::Trace:
        return trace;
        break;
    default:
        return none;
    }
}

uint64_t
ConsoleLogger::currentThreadId()
{
    return static_cast<uint64_t>( syscall( SYS_gettid ) );
}

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
