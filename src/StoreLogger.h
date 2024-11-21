// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/store/common/logging.hpp>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{
class Logger : public aws::store::logging::Logger
{
public:
    Logger();

    void log( aws::store::logging::LogLevel l, const std::string &message ) const override;
};
} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
