// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IoTFleetWiseConfig.h"
#include <boost/none.hpp>
#include <fstream>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

IoTFleetWiseConfig::IoTFleetWiseConfig( const Json::Value &config )
    : mConfig( config )
{
}

IoTFleetWiseConfig::IoTFleetWiseConfig( const Json::Value &config, std::string path )
    : mConfig( config )
    , mPath( std::move( path ) )
{
}

IoTFleetWiseConfig
IoTFleetWiseConfig::operator[]( unsigned index ) const
{
    return IoTFleetWiseConfig( mConfig[index], mPath + "[" + std::to_string( index ) + "]" );
}

IoTFleetWiseConfig
IoTFleetWiseConfig::operator[]( const std::string &key ) const
{
    return IoTFleetWiseConfig( mConfig[key], mPath + "." + key );
}

std::string
IoTFleetWiseConfig::getValueString() const
{
    return ( mConfig.isObject() || mConfig.isArray() ) ? "" : "'" + mConfig.asString() + "' ";
}

std::string
IoTFleetWiseConfig::asStringRequired() const
{
    if ( mConfig.isNull() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value missing at " + mPath );
    }
    if ( !mConfig.isString() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a string at " + mPath );
    }
    return mConfig.asString();
}

boost::optional<std::string>
IoTFleetWiseConfig::asStringOptional() const
{
    if ( mConfig.isNull() )
    {
        return boost::none;
    }
    if ( !mConfig.isString() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a string at " + mPath );
    }
    return boost::make_optional( mConfig.asString() );
}

uint32_t
IoTFleetWiseConfig::asU32Required() const
{
    if ( mConfig.isNull() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value missing at " + mPath );
    }
    if ( !mConfig.isUInt() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a valid uint32 at " + mPath );
    }
    return mConfig.asUInt();
}

boost::optional<uint32_t>
IoTFleetWiseConfig::asU32Optional() const
{
    if ( mConfig.isNull() )
    {
        return boost::none;
    }
    if ( !mConfig.isUInt() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a valid uint32 at " + mPath );
    }
    return boost::make_optional( mConfig.asUInt() );
}

uint64_t
IoTFleetWiseConfig::asU64Required() const
{
    if ( mConfig.isNull() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value missing at " + mPath );
    }
    if ( !mConfig.isUInt64() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a valid uint64 at " + mPath );
    }
    return mConfig.asUInt64();
}

boost::optional<uint64_t>
IoTFleetWiseConfig::asU64Optional() const
{
    if ( mConfig.isNull() )
    {
        return boost::none;
    }
    if ( !mConfig.isUInt64() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a valid uint64 at " + mPath );
    }
    return boost::make_optional( mConfig.asUInt64() );
}

uint32_t
IoTFleetWiseConfig::asU32FromStringRequired() const
{
    // coverity[fun_call_w_exception] False positive, if this function is not called, there won't be a catch for it
    auto value = asStringRequired();
    try
    {
        return static_cast<uint32_t>( std::stoul( value, nullptr, 0 ) );
    }
    catch ( ... )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Could not convert '" + value + "' to uint32 for config value at " + mPath );
    }
}

boost::optional<uint32_t>
IoTFleetWiseConfig::asU32FromStringOptional() const
{
    // coverity[fun_call_w_exception] False positive, if this function is not called, there won't be a catch for it
    auto value = asStringOptional();
    if ( !value.has_value() )
    {
        return boost::none;
    }
    try
    {
        return boost::make_optional( static_cast<uint32_t>( std::stoul( value.get(), nullptr, 0 ) ) );
    }
    catch ( ... )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__,
                            "Could not convert '" + value.get() + "' to uint32 for config value at " + mPath );
    }
}

size_t
IoTFleetWiseConfig::asSizeRequired() const
{
    if ( mConfig.isNull() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value missing at " + mPath );
    }
    if ( sizeof( size_t ) >= sizeof( uint64_t ) ? !mConfig.isUInt64() : !mConfig.isUInt() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a valid size at " + mPath );
    }
    return sizeof( size_t ) >= sizeof( uint64_t ) ? mConfig.asUInt64() : mConfig.asUInt();
}

boost::optional<size_t>
IoTFleetWiseConfig::asSizeOptional() const
{
    if ( mConfig.isNull() )
    {
        return boost::none;
    }
    if ( sizeof( size_t ) >= sizeof( uint64_t ) ? !mConfig.isUInt64() : !mConfig.isUInt() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a valid size at " + mPath );
    }
    return boost::make_optional(
        static_cast<size_t>( sizeof( size_t ) >= sizeof( uint64_t ) ? mConfig.asUInt64() : mConfig.asUInt() ) );
}

bool
IoTFleetWiseConfig::asBoolRequired() const
{
    if ( mConfig.isNull() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value missing at " + mPath );
    }
    if ( !mConfig.isBool() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a bool at " + mPath );
    }
    return mConfig.asBool();
}

boost::optional<bool>
IoTFleetWiseConfig::asBoolOptional() const
{
    if ( mConfig.isNull() )
    {
        return boost::none;
    }
    if ( !mConfig.isBool() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value " + getValueString() + "is not a bool at " + mPath );
    }
    return boost::make_optional( mConfig.asBool() );
}

bool
IoTFleetWiseConfig::isMember( const std::string &key ) const
{
    return mConfig.isMember( key );
}

unsigned int
IoTFleetWiseConfig::getArraySizeRequired() const
{
    if ( mConfig.isNull() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value missing at " + mPath );
    }
    if ( !mConfig.isArray() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value is not an array at " + mPath );
    }
    return mConfig.size();
}

unsigned int
IoTFleetWiseConfig::getArraySizeOptional() const
{
    if ( mConfig.isNull() )
    {
        return 0;
    }
    if ( !mConfig.isArray() )
    {
        // coverity[exception_thrown] False positive, if this function is not called, there won't be a catch for it
        throw runtimeError( __LINE__, "Config value is not an array at " + mPath );
    }
    return mConfig.size();
}

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

} // namespace IoTFleetWise
} // namespace Aws
