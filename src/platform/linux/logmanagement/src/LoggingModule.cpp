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

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
