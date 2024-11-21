// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StoreLogger.h"
#include "LogLevel.h"
#include <aws/store/common/logging.hpp>
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

class LoggerTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
    }

    void
    TearDown() override
    {
    }
};

TEST_F( LoggerTest, LevelTest )
{
    gSystemWideLogLevel = LogLevel::Off;
    Logger loggerOff;
    loggerOff.log( aws::store::logging::LogLevel::Error, "error when off test" );
    gSystemWideLogLevel = LogLevel::Error;
    Logger loggerError;
    loggerError.log( aws::store::logging::LogLevel::Error, "error when error test" );
    gSystemWideLogLevel = LogLevel::Warning;
    Logger loggerWarning;
    loggerWarning.log( aws::store::logging::LogLevel::Warning, "warning when warning test" );
    gSystemWideLogLevel = LogLevel::Info;
    Logger loggerInfo;
    loggerInfo.log( aws::store::logging::LogLevel::Info, "info when info test" );
    gSystemWideLogLevel = LogLevel::Trace;
    Logger loggerTrace;
    loggerTrace.log( aws::store::logging::LogLevel::Disabled, "disabled when trace test" );
    loggerTrace.log( aws::store::logging::LogLevel::Trace, "trace when trace test" );
    loggerTrace.log( aws::store::logging::LogLevel::Debug, "debug when trace test" );
    loggerTrace.log( aws::store::logging::LogLevel::Info, "info when trace test" );
    loggerTrace.log( aws::store::logging::LogLevel::Warning, "warning when trace test" );
    loggerTrace.log( aws::store::logging::LogLevel::Error, "error when trace test" );
}

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
