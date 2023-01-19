// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "LoggingModule.h"
#include <iomanip>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{
LoggingModule::LoggingModule() = default;

void
LoggingModule::error( const std::string &function, const std::string &logEntry )
{
    mLogger.logMessage( LogLevel::Error, function, logEntry );
}

void
LoggingModule::warn( const std::string &function, const std::string &logEntry )
{
    mLogger.logMessage( LogLevel::Warning, function, logEntry );
}

void
LoggingModule::info( const std::string &function, const std::string &logEntry )
{
    mLogger.logMessage( LogLevel::Info, function, logEntry );
}

void
LoggingModule::trace( const std::string &function, const std::string &logEntry )
{
    mLogger.logMessage( LogLevel::Trace, function, logEntry );
}

void
LoggingModule::traceBytesInVector( const std::string &function,
                                   const std::string &logEntry,
                                   const std::vector<uint8_t> &inputBytes )
{
    std::stringstream stream_bytes;

    // Push the elements of the array into the stream:
    // Add a prefix to the start of the output stream.
    // std::hex            - prints the item in hex representation
    // std::uppercase      - Makes the letters in the hex codes uppercase
    // std::setfill( '0' ) - Fills the width of each output with zeros
    // std::setw( ... )    - makes all of the output values 2 characters wide
    // static_cast< int >()- std::streams treat 'char' variables specially, so you need to cast to an int to see the
    // hex values
    for ( size_t i = 0; i < inputBytes.size(); i++ )
    {
        stream_bytes << std::hex << std::uppercase << std::setfill( '0' ) << std::setw( 2 * sizeof( uint8_t ) )
                     << static_cast<unsigned>( inputBytes[i] ) << " ";
    }
    const std::string logMsg = logEntry + ": " + stream_bytes.str();
    mLogger.logMessage( LogLevel::Trace, function, logMsg );
}

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
