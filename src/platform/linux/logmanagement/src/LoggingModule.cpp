// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "LoggingModule.h"

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
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX