// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/LogLevel.h"
#include <csignal>
#include <cstdint>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

// This is intended to be used in signal handlers to check whether a log was still being written
// when the signal was received, and decide if it is safe to flush the logs from a signal handler.
// coverity[autosar_cpp14_a2_10_4_violation:FALSE] False positive, this variable is declared only once.
// coverity[autosar_cpp14_a2_11_1_violation] volatile required as it will be used in a signal handler
extern volatile sig_atomic_t gOngoingLogMessage; // NOLINT Global signal

/**
 * @brief Recursive function to extract short filename from __FILE__ at compile time
 * @param path path to the file, e.g. __FILE__ in the beginning
 * @param lastFolder last parsed subfolder - full path to the file with subfolder in the beginning
 * @return extracted file name
 */
static constexpr const char *
// coverity[autosar_cpp14_a8_4_10_violation] raw pointer needed to match the expected signature
extractFilename( const char *path, const char *lastFolder )
{
    // Cannot iterate more, end of path, lastFolder is file
    if ( *path == '\0' )
    {
        return lastFolder;
    }
    // Next subfolder found, repeat recursion for the subfolder
    else if ( *path == '/' )
    {
        return extractFilename( path + 1, path + 1 );
    }
    // Move path pointer until new subfolder or end of path  will be found
    else
    {
        return extractFilename( path + 1, lastFolder );
    }
}

/**
 * @brief Get short filename from __FILE__ at compile time
 * @param path path to the file, e.g. __FILE__
 * @return extracted file name
 */
static constexpr const char *
getShortFilename( const char *path )
{
    return extractFilename( path, path );
}
/**
 * @brief Logging API. Used by all modules of the software to forward log entries
 * to a console logger instance.
 */
class LoggingModule
{
public:
    static const uint64_t LOG_AGGREGATION_TIME_MS =
        1000; /*< Periodic logging for one module should have a maximum frequency of this */

    /**
     * @brief Log function - calls ConsoleLogger
     * @param level log level
     * @param filename file that emitted the log
     * @param lineNumber line in the file
     * @param function calling function
     * @param logEntry actual message
     */
    static void log( LogLevel level,
                     const std::string &filename,
                     const uint32_t lineNumber,
                     const std::string &function,
                     const std::string &logEntry );

    /**
     * @brief Try to flush any log that has been buffered
     */
    static void flush();
};

/**
 * @brief Transforms bytes in vector to string
 * @param inputBytes bytes in vector to be printed
 * @return string of transformed bytes input
 */
std::string getStringFromBytes( const std::vector<uint8_t> &inputBytes );

/**
 * @brief Converts errno code to a string in a thread-safe way
 * @return string the error description
 */
std::string getErrnoString();

} // namespace IoTFleetWise
} // namespace Aws

/**
 * @brief Log INFO function
 * @param logEntry the message to be logged
 */
#define FWE_LOG_INFO( logEntry )                                                                                       \
    {                                                                                                                  \
        constexpr const char *shortFilename = Aws::IoTFleetWise::getShortFilename( __FILE__ );                         \
        Aws::IoTFleetWise::LoggingModule::log( Aws::IoTFleetWise::LogLevel::Info,                                      \
                                               shortFilename,                                                          \
                                               static_cast<uint16_t>( __LINE__ ),                                      \
                                               __FUNCTION__,                                                           \
                                               ( logEntry ) );                                                         \
    }

/**
 * @brief Log WARN function
 * @param logEntry the message to be logged
 */
#define FWE_LOG_WARN( logEntry )                                                                                       \
    {                                                                                                                  \
        constexpr const char *shortFilename = Aws::IoTFleetWise::getShortFilename( __FILE__ );                         \
        Aws::IoTFleetWise::LoggingModule::log( Aws::IoTFleetWise::LogLevel::Warning,                                   \
                                               shortFilename,                                                          \
                                               static_cast<uint16_t>( __LINE__ ),                                      \
                                               __FUNCTION__,                                                           \
                                               ( logEntry ) );                                                         \
    }

/**
 * @brief Log ERROR function
 * @param logEntry the message to be logged
 */
#define FWE_LOG_ERROR( logEntry )                                                                                      \
    {                                                                                                                  \
        constexpr const char *shortFilename = Aws::IoTFleetWise::getShortFilename( __FILE__ );                         \
        Aws::IoTFleetWise::LoggingModule::log( Aws::IoTFleetWise::LogLevel::Error,                                     \
                                               shortFilename,                                                          \
                                               static_cast<uint16_t>( __LINE__ ),                                      \
                                               __FUNCTION__,                                                           \
                                               ( logEntry ) );                                                         \
    }

/**
 * @brief Log TRACE function
 * @param logEntry the message to be logged
 */
#define FWE_LOG_TRACE( logEntry )                                                                                      \
    {                                                                                                                  \
        constexpr const char *shortFilename = Aws::IoTFleetWise::getShortFilename( __FILE__ );                         \
        Aws::IoTFleetWise::LoggingModule::log( Aws::IoTFleetWise::LogLevel::Trace,                                     \
                                               shortFilename,                                                          \
                                               static_cast<uint16_t>( __LINE__ ),                                      \
                                               __FUNCTION__,                                                           \
                                               ( logEntry ) );                                                         \
    }
