// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "IoTFleetWiseConfig.h"
#include <fstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace ExecutionManagement
{

bool
IoTFleetWiseConfig::read( const std::string &filename, Json::Value &config )
{
    try
    {
        std::ifstream configFileStream( filename );
        configFileStream >> config;
    }
    catch ( ... )
    {
        return false;
    }
    return true;
}

} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
