// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <boost/optional/optional.hpp>
#include <cstddef>
#include <cstdint>
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
    IoTFleetWiseConfig( const Json::Value &config );
    ~IoTFleetWiseConfig() = default;
    static bool read( const std::string &filename, Json::Value &config );

    IoTFleetWiseConfig operator[]( unsigned index ) const;
    IoTFleetWiseConfig operator[]( const std::string &key ) const;

    std::string asStringRequired() const;
    boost::optional<std::string> asStringOptional() const;

    uint32_t asU32Required() const;
    boost::optional<uint32_t> asU32Optional() const;

    uint64_t asU64Required() const;
    boost::optional<uint64_t> asU64Optional() const;

    uint32_t asU32FromStringRequired() const;
    boost::optional<uint32_t> asU32FromStringOptional() const;

    size_t asSizeRequired() const;
    boost::optional<size_t> asSizeOptional() const;

    bool asBoolRequired() const;
    boost::optional<bool> asBoolOptional() const;

    bool isMember( const std::string &key ) const;

    unsigned int getArraySizeRequired() const;
    unsigned int getArraySizeOptional() const;

private:
    IoTFleetWiseConfig( const Json::Value &config, std::string path );
    const Json::Value &mConfig;
    std::string mPath;
    std::string getValueString() const;
};

} // namespace IoTFleetWise
} // namespace Aws
