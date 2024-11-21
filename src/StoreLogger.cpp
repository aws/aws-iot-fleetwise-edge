// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StoreLogger.h"
#include "LogLevel.h"
#include "LoggingModule.h"
#include <aws/store/common/logging.hpp>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

Logger::Logger()
{
    switch ( gSystemWideLogLevel )
    {
    case LogLevel::Trace:
        level = aws::store::logging::LogLevel::Trace;
        break;
    case LogLevel::Info:
        level = aws::store::logging::LogLevel::Info;
        break;
    case LogLevel::Warning:
        level = aws::store::logging::LogLevel::Warning;
        break;
    case LogLevel::Error:
        level = aws::store::logging::LogLevel::Error;
        break;
    case LogLevel::Off:
        level = aws::store::logging::LogLevel::Disabled;
        break;
    }
}

void
Logger::log( aws::store::logging::LogLevel l, const std::string &message ) const
{
    switch ( l )
    {
    case aws::store::logging::LogLevel::Disabled:
        break;
    case aws::store::logging::LogLevel::Trace:
        LoggingModule::log( LogLevel::Trace, {}, 0, {}, message );
        break;
    case aws::store::logging::LogLevel::Debug:
        LoggingModule::log( LogLevel::Trace, {}, 0, {}, message );
        break;
    case aws::store::logging::LogLevel::Info:
        LoggingModule::log( LogLevel::Info, {}, 0, {}, message );
        break;
    case aws::store::logging::LogLevel::Warning:
        LoggingModule::log( LogLevel::Warning, {}, 0, {}, message );
        break;
    case aws::store::logging::LogLevel::Error:
        LoggingModule::log( LogLevel::Error, {}, 0, {}, message );
        break;
    }
}
} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
