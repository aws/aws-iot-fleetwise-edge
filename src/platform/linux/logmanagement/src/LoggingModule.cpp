/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "LoggingModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{

LoggingModule::LoggingModule()
{
}

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

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX