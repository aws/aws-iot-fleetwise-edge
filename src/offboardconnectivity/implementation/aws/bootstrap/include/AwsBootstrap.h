
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
#pragma once

#include <memory>

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
namespace OffboardConnectivityAwsIot
{

/**
 * This module bootstrap the AWS CRT by creating event loop and client bootstrap
 */
class AwsBootstrap
{
public:
    ~AwsBootstrap();

    AwsBootstrap( const AwsBootstrap & ) = delete;
    AwsBootstrap &operator=( const AwsBootstrap & ) = delete;
    AwsBootstrap( AwsBootstrap && ) = delete;
    AwsBootstrap &operator=( AwsBootstrap && ) = delete;

    // initialization on first use (https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#i3-avoid-singletons)
    static AwsBootstrap &getInstance();

    /**
     * @brief Create eventLoopGroup, DefaultHostResolver and clientBoostrap
     * Note this function use std::call_once so that the all resources will be created only once
     *
     * @return raw pointer to Aws::Crt::Io::ClientBootstrap
     */
    Aws::Crt::Io::ClientBootstrap *getClientBootStrap();

private:
    AwsBootstrap();

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws