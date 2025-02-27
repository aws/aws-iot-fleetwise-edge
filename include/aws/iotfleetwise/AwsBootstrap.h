// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/core/utils/logging/LogLevel.h>
#include <memory>
#include <string>

namespace Aws
{
namespace Crt
{
namespace Io
{
class ClientBootstrap;
}
} // namespace Crt

namespace IoTFleetWise
{

/**
 * This module bootstrap the AWS CRT by creating event loop and client bootstrap
 */
class AwsBootstrap
{
public:
    ~AwsBootstrap() = default;

    AwsBootstrap( const AwsBootstrap & ) = delete;
    AwsBootstrap &operator=( const AwsBootstrap & ) = delete;
    AwsBootstrap( AwsBootstrap && ) = delete;
    AwsBootstrap &operator=( AwsBootstrap && ) = delete;

    // initialization on first use (https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#i3-avoid-singletons)
    static AwsBootstrap &getInstance( Aws::Utils::Logging::LogLevel logLevel = Aws::Utils::Logging::LogLevel::Off );

    static Aws::Utils::Logging::LogLevel logLevelFromString( const std::string &logLevelStr );

    /**
     * @brief Create eventLoopGroup, DefaultHostResolver and clientBoostrap
     * Note this function use std::call_once so that the all resources will be created only once
     *
     * @return raw pointer to Aws::Crt::Io::ClientBootstrap
     */
    Aws::Crt::Io::ClientBootstrap *getClientBootStrap();

private:
    AwsBootstrap( Aws::Utils::Logging::LogLevel logLevel );

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace IoTFleetWise
} // namespace Aws
