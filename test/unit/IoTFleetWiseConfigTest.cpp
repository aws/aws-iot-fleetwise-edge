
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IoTFleetWiseConfig.h"
#include "Testing.h"
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <json/json.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

TEST( IoTFleetWiseConfigTest, BadFileName )
{
    Json::Value config;
    ASSERT_FALSE( IoTFleetWiseConfig::read( "bad-file-name.json", config ) );
}

TEST( IoTFleetWiseConfigTest, ReadOk )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-ok.json", config ) );
    ASSERT_EQ( 10000, config["staticConfig"]["bufferSizes"]["decodedSignalsBufferSize"].asInt() );
    ASSERT_EQ( "Trace", config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString() );
}

TEST( IoTFleetWiseConfigTest, ConfigValues )
{
    Json::Value jsonConfig;
    size_t mySize = static_cast<size_t>( -1 );
    jsonConfig["my_string"] = "abc";
    jsonConfig["my_int32"] = -123;
    jsonConfig["my_uint32"] = 123U;
    jsonConfig["my_uint64"] = UINT64_MAX;
    jsonConfig["my_float32"] = 123.456;
    jsonConfig["my_uint32_string"] = "123";
    jsonConfig["my_uint32_string_hex"] = "0x123";
    jsonConfig["my_size"] = mySize;
    jsonConfig["my_bool"] = true;
    jsonConfig["my_obj"]["a"] = 1;
    jsonConfig["my_array"][0] = 1;

    IoTFleetWiseConfig config( jsonConfig );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].asStringRequired(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].asStringRequired(), "Config value '123' is not a string at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asStringRequired(), "Config value is not a string at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asStringRequired(), "Config value is not a string at .my_array" );
    EXPECT_EQ( config["my_string"].asStringRequired(), "abc" );

    EXPECT_EQ( config["a"][0]["b"].asStringOptional(), boost::none );
    EXPECT_THROW_MESSAGE( config["my_uint32"].asStringOptional(), "Config value '123' is not a string at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asStringOptional(), "Config value is not a string at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asStringOptional(), "Config value is not a string at .my_array" );
    EXPECT_EQ( config["my_string"].asStringOptional().get(), "abc" );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].asU32Required(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_string"].asU32Required(),
                          "Config value 'abc' is not a valid uint32 at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asU32Required(),
                          "Config value '-123' is not a valid uint32 at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asU32Required(),
                          "Config value '123.456' is not a valid uint32 at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asU32Required(), "Config value is not a valid uint32 at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asU32Required(), "Config value is not a valid uint32 at .my_array" );
    EXPECT_EQ( config["my_uint32"].asU32Required(), 123 );

    EXPECT_EQ( config["a"][0]["b"].asU32Optional(), boost::none );
    EXPECT_THROW_MESSAGE( config["my_string"].asU32Optional(),
                          "Config value 'abc' is not a valid uint32 at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asU32Optional(),
                          "Config value '-123' is not a valid uint32 at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asU32Optional(),
                          "Config value '123.456' is not a valid uint32 at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asU32Optional(), "Config value is not a valid uint32 at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asU32Optional(), "Config value is not a valid uint32 at .my_array" );
    EXPECT_EQ( config["my_uint32"].asU32Optional().get(), 123 );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].asU64Required(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_string"].asU64Required(),
                          "Config value 'abc' is not a valid uint64 at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asU64Required(),
                          "Config value '-123' is not a valid uint64 at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asU64Required(),
                          "Config value '123.456' is not a valid uint64 at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asU64Required(), "Config value is not a valid uint64 at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asU64Required(), "Config value is not a valid uint64 at .my_array" );
    EXPECT_EQ( config["my_uint64"].asU64Required(), UINT64_MAX );

    EXPECT_EQ( config["a"][0]["b"].asU64Optional(), boost::none );
    EXPECT_THROW_MESSAGE( config["my_string"].asU64Optional(),
                          "Config value 'abc' is not a valid uint64 at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asU64Optional(),
                          "Config value '-123' is not a valid uint64 at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asU64Optional(),
                          "Config value '123.456' is not a valid uint64 at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asU64Optional(), "Config value is not a valid uint64 at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asU64Optional(), "Config value is not a valid uint64 at .my_array" );
    EXPECT_EQ( config["my_uint64"].asU64Optional().get(), UINT64_MAX );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].asU32FromStringRequired(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_string"].asU32FromStringRequired(),
                          "Could not convert 'abc' to uint32 for config value at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].asU32FromStringRequired(),
                          "Config value '123' is not a string at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asU32FromStringRequired(),
                          "Config value '-123' is not a string at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asU32FromStringRequired(),
                          "Config value '123.456' is not a string at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asU32FromStringRequired(), "Config value is not a string at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asU32FromStringRequired(), "Config value is not a string at .my_array" );
    EXPECT_EQ( config["my_uint32_string"].asU32FromStringRequired(), 123 );
    EXPECT_EQ( config["my_uint32_string_hex"].asU32FromStringRequired(), 0x123 );

    EXPECT_EQ( config["a"][0]["b"].asU32FromStringOptional(), boost::none );
    EXPECT_THROW_MESSAGE( config["my_string"].asU32FromStringOptional(),
                          "Could not convert 'abc' to uint32 for config value at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].asU32FromStringOptional(),
                          "Config value '123' is not a string at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asU32FromStringOptional(),
                          "Config value '-123' is not a string at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asU32FromStringOptional(),
                          "Config value '123.456' is not a string at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asU32FromStringOptional(), "Config value is not a string at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asU32FromStringOptional(), "Config value is not a string at .my_array" );
    EXPECT_EQ( config["my_uint32_string"].asU32FromStringOptional().get(), 123 );
    EXPECT_EQ( config["my_uint32_string_hex"].asU32FromStringOptional().get(), 0x123 );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].asSizeRequired(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_string"].asSizeRequired(),
                          "Config value 'abc' is not a valid size at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asSizeRequired(), "Config value '-123' is not a valid size at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asSizeRequired(),
                          "Config value '123.456' is not a valid size at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asSizeRequired(), "Config value is not a valid size at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asSizeRequired(), "Config value is not a valid size at .my_array" );
    EXPECT_EQ( config["my_size"].asSizeRequired(), mySize );

    EXPECT_EQ( config["a"][0]["b"].asSizeOptional(), boost::none );
    EXPECT_THROW_MESSAGE( config["my_string"].asSizeOptional(),
                          "Config value 'abc' is not a valid size at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asSizeOptional(), "Config value '-123' is not a valid size at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asSizeOptional(),
                          "Config value '123.456' is not a valid size at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asSizeOptional(), "Config value is not a valid size at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asSizeOptional(), "Config value is not a valid size at .my_array" );
    EXPECT_EQ( config["my_size"].asSizeOptional(), mySize );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].asBoolRequired(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_string"].asBoolRequired(), "Config value 'abc' is not a bool at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].asBoolRequired(), "Config value '123' is not a bool at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asBoolRequired(), "Config value '-123' is not a bool at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asBoolRequired(),
                          "Config value '123.456' is not a bool at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asBoolRequired(), "Config value is not a bool at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asBoolRequired(), "Config value is not a bool at .my_array" );
    EXPECT_EQ( config["my_bool"].asBoolRequired(), true );

    EXPECT_EQ( config["a"][0]["b"].asBoolOptional(), boost::none );
    EXPECT_THROW_MESSAGE( config["my_string"].asBoolOptional(), "Config value 'abc' is not a bool at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].asBoolOptional(), "Config value '123' is not a bool at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_int32"].asBoolOptional(), "Config value '-123' is not a bool at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].asBoolOptional(),
                          "Config value '123.456' is not a bool at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].asBoolOptional(), "Config value is not a bool at .my_obj" );
    EXPECT_THROW_MESSAGE( config["my_array"].asBoolOptional(), "Config value is not a bool at .my_array" );
    EXPECT_EQ( config["my_bool"].asBoolOptional().get(), true );

    EXPECT_FALSE( config.isMember( "a" ) );
    EXPECT_TRUE( config.isMember( "my_string" ) );

    EXPECT_THROW_MESSAGE( config["a"][0]["b"].getArraySizeRequired(), "Config value missing at .a[0].b" );
    EXPECT_THROW_MESSAGE( config["my_string"].getArraySizeRequired(), "Config value is not an array at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].getArraySizeRequired(), "Config value is not an array at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_int32"].getArraySizeRequired(), "Config value is not an array at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].getArraySizeRequired(), "Config value is not an array at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].getArraySizeRequired(), "Config value is not an array at .my_obj" );
    EXPECT_EQ( config["my_array"].getArraySizeRequired(), 1 );

    EXPECT_EQ( config["a"][0]["b"].getArraySizeOptional(), 0 );
    EXPECT_THROW_MESSAGE( config["my_string"].getArraySizeOptional(), "Config value is not an array at .my_string" );
    EXPECT_THROW_MESSAGE( config["my_uint32"].getArraySizeOptional(), "Config value is not an array at .my_uint32" );
    EXPECT_THROW_MESSAGE( config["my_int32"].getArraySizeOptional(), "Config value is not an array at .my_int32" );
    EXPECT_THROW_MESSAGE( config["my_float32"].getArraySizeOptional(), "Config value is not an array at .my_float32" );
    EXPECT_THROW_MESSAGE( config["my_obj"].getArraySizeOptional(), "Config value is not an array at .my_obj" );
    EXPECT_EQ( config["my_array"].getArraySizeOptional(), 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
