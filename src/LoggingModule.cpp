// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LoggingModule.h"
#include "ConsoleLogger.h"
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{

void
LoggingModule::log( LogLevel level,
                    const std::string &filename,
                    const uint32_t lineNumber,
                    const std::string &function,
                    const std::string &logEntry )
{
    static ConsoleLogger logger;
    logger.logMessage( level, filename, lineNumber, function, logEntry );
}

std::string
getStringFromBytes( const std::vector<uint8_t> &inputBytes )
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
    return stream_bytes.str();
}

std::string
getErrnoString()
{
    // coverity[autosar_cpp14_m19_3_1_violation]
    // coverity[misra_cpp_2008_rule_19_3_1_violation] using errno for logging purposes only
    int lastError = errno;

    char buf[256];
    auto result = strerror_r( lastError, &buf[0], sizeof( buf ) );

#if defined _GNU_SOURCE && !defined __ANDROID__
    // On Linux, there is an issue with libstdc++ that doesn't allow us to use the POSIX compliant
    // strerror_r function (which would require us to define _XOPEN_SOURCE, but since _GNU_SOURCE is
    // always defined, we always get the GNU version):
    // https://stackoverflow.com/questions/11670581/why-is-gnu-source-defined-by-default-and-how-to-turn-it-off
    if ( result != nullptr )
    {
        return std::string( result );
    }
#else
    if ( result == 0 )
    {
        return std::string( &buf[0] );
    }
#endif
    FWE_LOG_ERROR( "Could not get string for errno: " + std::to_string( lastError ) );
    return std::to_string( lastError );
}

} // namespace IoTFleetWise
} // namespace Aws
