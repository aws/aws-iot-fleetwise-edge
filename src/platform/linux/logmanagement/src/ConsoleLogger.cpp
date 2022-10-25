// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "ConsoleLogger.h"
#include "ClockHandler.h"
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
LogLevel gSystemWideLogLevel;

static std::mutex gLogForwardingMutex;
static ILogger *gLogForwarder = nullptr;

void
setLogForwarding( ILogger *logForwarder )
{
    std::lock_guard<std::mutex> lock( gLogForwardingMutex );
    gLogForwarder = logForwarder;
}

void
forwardLog( LogLevel level, const std::string &function, const std::string &logEntry )
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
        gLogForwarder->logMessage( level, function, logEntry );
    }
}

void
ConsoleLogger::logMessage( LogLevel level, const std::string &function, const std::string &logEntry )
{
    if ( level >= gSystemWideLogLevel )
    {
        std::printf( "[Thread : %" PRIu64 "] [%s] [%s] [%s]: [%s] ",
                     currentThreadId(),
                     timeAsString().c_str(),
                     levelToString( level ).c_str(),
                     function.c_str(),
                     logEntry.c_str() );
        std::printf( "\n" );
        std::fflush( stdout );
        forwardLog( level, function, logEntry );
    }
}

std::string
ConsoleLogger::timeAsString()
{
    auto clock = ClockHandler::getClock();
    return clock->timestampToString();
}

const std::string &
ILogger::levelToString( LogLevel level )
{
    static const std::string error( "ERROR" );
    static const std::string warn( "WARN" );
    static const std::string info( "INFO" );
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
