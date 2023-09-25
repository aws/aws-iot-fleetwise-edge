// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <json/json.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/** @brief FWE Configuration.
 *  Reads FWE's static configuration from a JSON file. */
class IoTFleetWiseConfig
{
public:
    IoTFleetWiseConfig() = delete;
    static bool read( const std::string &filename, Json::Value &config );

private:
};

} // namespace IoTFleetWise
} // namespace Aws
