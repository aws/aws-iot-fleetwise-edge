// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <boost/filesystem.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#define EXPECT_THROW_MESSAGE( x, message )                                                                             \
    try                                                                                                                \
    {                                                                                                                  \
        x;                                                                                                             \
        ADD_FAILURE() << "No exception was thrown";                                                                    \
    }                                                                                                                  \
    catch ( const std::exception &e )                                                                                  \
    {                                                                                                                  \
        std::string actualMessage( e.what() );                                                                         \
        std::string expectedMessage( message );                                                                        \
        EXPECT_EQ( actualMessage, expectedMessage ) << "Unexpected exception message";                                 \
    }

namespace Aws
{
namespace IoTFleetWise
{

inline TimePoint
operator+( const TimePoint &time, Timestamp increment )
{
    return { time.systemTimeMs + increment, time.monotonicTimeMs + increment };
}

inline TimePoint &
operator+=( TimePoint &time, Timestamp increment )
{
    time.systemTimeMs += increment;
    time.monotonicTimeMs += increment;
    return time;
}

inline TimePoint
operator++( TimePoint &time, int )
{
    time += 1;
    return time;
}

inline bool
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

inline void
assertSignalValue( const SignalValueWrapper &signalValueWrapper,
                   double expectedSignalValue,
                   SignalType expectedSignalType )
{
    switch ( expectedSignalType )
    {
    case SignalType::UINT8:
        ASSERT_EQ( signalValueWrapper.value.uint8Val, static_cast<uint8_t>( expectedSignalValue ) );
        break;
    case SignalType::INT8:
        ASSERT_EQ( signalValueWrapper.value.int8Val, static_cast<int8_t>( expectedSignalValue ) );
        break;
    case SignalType::UINT16:
        ASSERT_EQ( signalValueWrapper.value.uint16Val, static_cast<uint16_t>( expectedSignalValue ) );
        break;
    case SignalType::INT16:
        ASSERT_EQ( signalValueWrapper.value.int16Val, static_cast<int16_t>( expectedSignalValue ) );
        break;
    case SignalType::UINT32:
        ASSERT_EQ( signalValueWrapper.value.uint32Val, static_cast<uint32_t>( expectedSignalValue ) );
        break;
    case SignalType::INT32:
        ASSERT_EQ( signalValueWrapper.value.int32Val, static_cast<int32_t>( expectedSignalValue ) );
        break;
    case SignalType::UINT64:
        ASSERT_EQ( signalValueWrapper.value.uint64Val, static_cast<uint64_t>( expectedSignalValue ) );
        break;
    case SignalType::INT64:
        ASSERT_EQ( signalValueWrapper.value.int64Val, static_cast<int64_t>( expectedSignalValue ) );
        break;
    case SignalType::FLOAT:
        ASSERT_FLOAT_EQ( signalValueWrapper.value.floatVal, static_cast<float>( expectedSignalValue ) );
        break;
    case SignalType::DOUBLE:
        ASSERT_DOUBLE_EQ( signalValueWrapper.value.doubleVal, static_cast<double>( expectedSignalValue ) );
        break;
    case SignalType::BOOLEAN:
        ASSERT_EQ( signalValueWrapper.value.boolVal, static_cast<bool>( expectedSignalValue ) );
        break;
    default:
        FAIL() << "Unsupported signal type";
    }
}

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
inline std::string
signalTypeParamInfoToString( const testing::TestParamInfo<SignalType> &info )
{
    SignalType signalType = info.param;
    return signalTypeToString( signalType );
}

constexpr inline std::size_t operator""_KiB( unsigned long long sizeBytes )
{
    return static_cast<size_t>( sizeBytes * 1024 );
}

constexpr inline std::size_t operator""_MiB( unsigned long long sizeBytes )
{
    return static_cast<size_t>( sizeBytes * 1024 * 1024 );
}

constexpr inline std::size_t operator""_GiB( unsigned long long sizeBytes )
{
    return static_cast<size_t>( sizeBytes * 1024 * 1024 * 1024 );
}

inline boost::filesystem::path
getTempDir()
{
    auto testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
    auto testDir = boost::filesystem::current_path() / "unit_tests_tmp" / testInfo->test_case_name() / testInfo->name();
    boost::filesystem::create_directories( testDir );
    return testDir;
}

inline std::shared_ptr<CacheAndPersist>
createCacheAndPersist()
{
    auto cacheAndPersist = std::make_shared<CacheAndPersist>( getTempDir().string(), 131072 );
    if ( !cacheAndPersist->init() )
    {
        // throw instead of using ASSERT_TRUE because the caller would have to use ASSERT_NO_FATAL_FAILURE
        // in order for the assert to work.
        throw std::runtime_error( "Failed to initialize persistency" );
    }
    return cacheAndPersist;
}

inline boost::optional<std::string>
getEnvVar( const std::string &name )
{
    const char *envVarValue = getenv( name.c_str() );
    if ( envVarValue == nullptr )
    {
        return boost::none;
    }
    return std::string( envVarValue );
}

inline unsigned int
getWorkerNumber()
{
    // When running the unit tests in parallel using pytest-cpp and pytest-xdist, this variable
    // should be present with the name of the worker and a sequential number. We can then use this
    // number to select different CAN interfaces so that tests are isolated from each other.
    auto workerName = getEnvVar( "PYTEST_XDIST_WORKER" ).get_value_or( "gw0" );
    FWE_LOG_TRACE( "Worker name: " + workerName )
    try
    {
        int workerNumber = stoi( workerName.substr( 2 ) );
        if ( workerNumber < 0 )
        {
            throw std::runtime_error( "Invalid worker number: " + std::to_string( workerNumber ) );
        }
        return static_cast<unsigned int>( workerNumber );
    }
    catch ( const std::invalid_argument &e )
    {
        throw std::runtime_error( "Invalid worker name: " + workerName +
                                  " . It should be in the format gw<SEQUENCE_NUMBER>" );
    }
}

inline std::string
getCanInterfaceName()
{
    std::string canInterface = "vcan" + std::to_string( getWorkerNumber() );
    FWE_LOG_TRACE( "Using CAN interface: " + canInterface )
    return canInterface;
}

} // namespace IoTFleetWise
} // namespace Aws
