// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "SignalTypes.h"
#include "TimeTypes.h"
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace TestingSupport
{

using TimePoint = Aws::IoTFleetWise::Platform::Linux::TimePoint;
using Timestamp = Aws::IoTFleetWise::Platform::Linux::Timestamp;
using SignalType = Aws::IoTFleetWise::DataManagement::SignalType;

TimePoint
operator+( const TimePoint &time, Timestamp increment )
{
    return { time.systemTimeMs + increment, time.monotonicTimeMs + increment };
}

TimePoint &
operator+=( TimePoint &time, Timestamp increment )
{
    time.systemTimeMs += increment;
    time.monotonicTimeMs += increment;
    return time;
}

TimePoint
operator++( TimePoint &time, int )
{
    time += 1;
    return time;
}

bool
operator==( const TimePoint &left, const TimePoint &right )
{
    return left.systemTimeMs == right.systemTimeMs && left.monotonicTimeMs == right.monotonicTimeMs;
}

template <typename T>
constexpr SignalType
getSignalType()
{
    return SignalType::DOUBLE;
}

template <>
constexpr SignalType
getSignalType<uint8_t>()
{
    return SignalType::UINT8;
}

template <>
constexpr SignalType
getSignalType<int8_t>()
{
    return SignalType::INT8;
}

template <>
constexpr SignalType
getSignalType<uint16_t>()
{
    return SignalType::UINT16;
}

template <>
constexpr SignalType
getSignalType<int16_t>()
{
    return SignalType::INT16;
}
template <>
constexpr SignalType
getSignalType<uint32_t>()
{
    return SignalType::UINT32;
}

template <>
constexpr SignalType
getSignalType<int32_t>()
{
    return SignalType::INT32;
}

template <>
constexpr SignalType
getSignalType<uint64_t>()
{
    return SignalType::UINT64;
}

template <>
constexpr SignalType
getSignalType<int64_t>()
{
    return SignalType::INT64;
}

template <>
constexpr SignalType
getSignalType<float>()
{
    return SignalType::FLOAT;
}

template <>
constexpr SignalType
getSignalType<double>()
{
    return SignalType::DOUBLE;
}

template <>
constexpr SignalType
getSignalType<bool>()
{
    return SignalType::BOOLEAN;
}

const auto allSignalTypes = testing::Values( SignalType::UINT8,
                                             SignalType::INT8,
                                             SignalType::UINT16,
                                             SignalType::INT16,
                                             SignalType::UINT32,
                                             SignalType::INT32,
                                             SignalType::UINT64,
                                             SignalType::INT64,
                                             SignalType::FLOAT,
                                             SignalType::DOUBLE,
                                             SignalType::BOOLEAN );

const auto signedSignalTypes = testing::Values(
    SignalType::INT8, SignalType::INT16, SignalType::INT32, SignalType::INT64, SignalType::FLOAT, SignalType::DOUBLE );

/**
 * @brief Converts SignalType to a string to be used in parametrized tests
 *
 * For more details, see:
 *
 * http://google.github.io/googletest/advanced.html#specifying-names-for-value-parameterized-test-parameters
 *
 * @param info
 * @return A string that can be used as the parameter name
 */
std::string
signalTypeToString( const testing::TestParamInfo<SignalType> &info )
{
    SignalType signalType = info.param;
    switch ( signalType )
    {
    case SignalType::UINT8:
        return "UINT8";
    case SignalType::INT8:
        return "INT8";
    case SignalType::UINT16:
        return "UINT16";
    case SignalType::INT16:
        return "INT16";
    case SignalType::UINT32:
        return "UINT32";
    case SignalType::INT32:
        return "INT32";
    case SignalType::UINT64:
        return "UINT64";
    case SignalType::INT64:
        return "INT64";
    case SignalType::FLOAT:
        return "FLOAT";
    case SignalType::DOUBLE:
        return "DOUBLE";
    case SignalType::BOOLEAN:
        return "BOOLEAN";
    default:
        throw std::invalid_argument( "Unsupported signal type" );
    }
}

std::size_t operator""_KiB( unsigned long long sizeBytes )
{
    return static_cast<size_t>( sizeBytes * 1024 );
}

std::size_t operator""_MiB( unsigned long long sizeBytes )
{
    return static_cast<size_t>( sizeBytes * 1024 * 1024 );
}

std::size_t operator""_GiB( unsigned long long sizeBytes )
{
    return static_cast<size_t>( sizeBytes * 1024 * 1024 * 1024 );
}

} // namespace TestingSupport
} // namespace IoTFleetWise
} // namespace Aws
