
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CanCommandDispatcher.h"
#include "RawDataBufferManagerSpy.h"
#include "Testing.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <boost/optional/optional.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <memory>
#include <net/if.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::NiceMock;

class CanCommandDispatcherTest : public ::testing::Test
{
protected:
    const Timestamp TIMEOUT_MS = 500;
    const std::string CAN_INTERFACE_NAME = getCanInterfaceName();
    std::unordered_map<std::string, CanCommandDispatcher::CommandConfig> mConfig = {
        { "Vehicle.actuator1", { 0x100, 0x101, SignalType::UINT8 } },
        { "Vehicle.actuator2", { 0x200, 0x201, SignalType::INT8 } },
        { "Vehicle.actuator3", { 0x300, 0x301, SignalType::UINT16 } },
        { "Vehicle.actuator4", { 0x400, 0x401, SignalType::INT16 } },
        { "Vehicle.actuator5", { 0x500, 0x501, SignalType::UINT32 } },
        { "Vehicle.actuator6", { 0x600, 0x601, SignalType::INT32 } },
        { "Vehicle.actuator7", { 0x700, 0x701, SignalType::UINT64 } },
        { "Vehicle.actuator8", { 0x800, 0x801, SignalType::INT64 } },
        { "Vehicle.actuator9", { 0x900, 0x901, SignalType::FLOAT } },
        { "Vehicle.actuator10", { 0xA00, 0xA01, SignalType::DOUBLE } },
        { "Vehicle.actuator11", { 0xB00, 0xB01, SignalType::BOOLEAN } },
        { "Vehicle.actuator12", { 0xC00, 0xC01, SignalType::STRING } },
        { "Vehicle.actuator13", { 0xC00, 0xC01, static_cast<SignalType>( -1 ) } },
    };
    using MockNotifyCommandStatusCallback = MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>;

    NiceMock<Testing::RawDataBufferManagerSpy> mRawDataBufferManagerSpy;

    CanCommandDispatcherTest()
        : mRawDataBufferManagerSpy( RawData::BufferManagerConfig::create().get() )
    {
    }

    void
    SetUp() override
    {
        if ( !setup() )
        {
            GTEST_FAIL() << "Test failed due to unavailability of socket";
        }
    }

    void
    TearDown() override
    {
        cleanUp();
    }

    int mCanSocket;

    void
    cleanUp()
    {
        close( mCanSocket );
    }

    bool
    setup()
    {
        mCanSocket = socket( PF_CAN, SOCK_RAW, CAN_RAW );
        if ( mCanSocket < 0 )
        {
            return false;
        }
        int canFdOn = 1;
        if ( setsockopt( mCanSocket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canFdOn, sizeof( canFdOn ) ) != 0 )
        {
            cleanUp();
            return false;
        }
        auto interfaceIndex = if_nametoindex( CAN_INTERFACE_NAME.c_str() );
        if ( interfaceIndex == 0 )
        {
            cleanUp();
            return false;
        }
        struct sockaddr_can interfaceAddress = {};
        interfaceAddress.can_family = AF_CAN;
        interfaceAddress.can_ifindex = static_cast<int>( interfaceIndex );
        if ( bind( mCanSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
        {
            cleanUp();
            return false;
        }
        return true;
    }

    void
    sendMessage( uint32_t id, const std::vector<uint8_t> &data )
    {
        struct canfd_frame frame = {};
        frame.can_id = id;
        if ( data.size() > CANFD_MAX_DLEN )
        {
            throw std::runtime_error( "data size too big" );
        }
        frame.len = static_cast<uint8_t>( data.size() );
        memcpy( frame.data, data.data(), data.size() );
        auto bytesWritten = write( mCanSocket, &frame, sizeof( struct canfd_frame ) );
        if ( bytesWritten != sizeof( struct canfd_frame ) )
        {
            throw std::runtime_error( "error writing CAN frame: expected to write " +
                                      std::to_string( sizeof( struct canfd_frame ) ) + " bytes, but wrote " +
                                      std::to_string( bytesWritten ) );
        }
    }

    void
    receiveMessage( uint32_t &id, std::vector<uint8_t> &data )
    {
        struct pollfd pfd = { mCanSocket, POLLIN, 0 };
        int res = poll( &pfd, 1U, static_cast<int>( TIMEOUT_MS ) );
        if ( res < 0 )
        {
            throw std::runtime_error( "Error reading from CAN" );
        }
        if ( res == 0 )
        {
            throw std::runtime_error( "Timeout waiting for response" );
        }
        struct canfd_frame frame = {};
        uint8_t *buffer = reinterpret_cast<uint8_t *>( &frame );

        size_t totalBytesRead = 0;
        size_t frameSize = sizeof( struct canfd_frame );
        auto startTime = ClockHandler::getClock()->monotonicTimeSinceEpochMs();
        while ( totalBytesRead < frameSize )
        {
            if ( ( ClockHandler::getClock()->monotonicTimeSinceEpochMs() - startTime ) > TIMEOUT_MS )
            {
                throw std::runtime_error( "Timeout reading response" );
            }

            auto bytesRead = read( mCanSocket, buffer + totalBytesRead, frameSize - totalBytesRead );
            if ( bytesRead < 0 )
            {
                throw std::runtime_error( "Error reading from CAN" );
            }
            totalBytesRead += static_cast<size_t>( bytesRead );
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }

        id = frame.can_id;
        data.assign( frame.data, frame.data + frame.len );
    }

    void
    testSuccessful( std::string actuatorName, SignalValueWrapper value, std::vector<uint8_t> &data )
    {
        CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
        ASSERT_TRUE( dispatcher.init() );
        MockNotifyCommandStatusCallback callback1;
        std::promise<void> callbackPromise;
        EXPECT_CALL( callback1, Call( _, _, _ ) )
            .Times( 1 )
            .WillOnce( Invoke( [&callbackPromise]( CommandStatus status,
                                                   CommandReasonCode reasonCode,
                                                   const CommandReasonDescription &reasonDescription ) {
                EXPECT_EQ( status, CommandStatus::SUCCEEDED );
                EXPECT_EQ( reasonCode, 0x11223344 );
                EXPECT_EQ( reasonDescription, "cat" );
                callbackPromise.set_value();
            } ) );
        auto issuedTimestamp = ClockHandler::getClock()->systemTimeSinceEpochMs();
        dispatcher.setActuatorValue(
            actuatorName, value, "ABC", issuedTimestamp, TIMEOUT_MS, callback1.AsStdFunction() );
        uint32_t id;
        receiveMessage( id, data );
        EXPECT_EQ( id, mConfig[actuatorName].canRequestId );
        ASSERT_GE( data.size(), 20 );
        EXPECT_EQ( data[0], 'A' );
        EXPECT_EQ( data[1], 'B' );
        EXPECT_EQ( data[2], 'C' );
        EXPECT_EQ( data[3], 0x00 );
        EXPECT_EQ( data[4], static_cast<uint8_t>( issuedTimestamp >> 56 ) );
        EXPECT_EQ( data[5], static_cast<uint8_t>( issuedTimestamp >> 48 ) );
        EXPECT_EQ( data[6], static_cast<uint8_t>( issuedTimestamp >> 40 ) );
        EXPECT_EQ( data[7], static_cast<uint8_t>( issuedTimestamp >> 32 ) );
        EXPECT_EQ( data[8], static_cast<uint8_t>( issuedTimestamp >> 24 ) );
        EXPECT_EQ( data[9], static_cast<uint8_t>( issuedTimestamp >> 16 ) );
        EXPECT_EQ( data[10], static_cast<uint8_t>( issuedTimestamp >> 8 ) );
        EXPECT_EQ( data[11], static_cast<uint8_t>( issuedTimestamp ) );
        EXPECT_EQ( data[12], static_cast<uint8_t>( TIMEOUT_MS >> 56 ) );
        EXPECT_EQ( data[13], static_cast<uint8_t>( TIMEOUT_MS >> 48 ) );
        EXPECT_EQ( data[14], static_cast<uint8_t>( TIMEOUT_MS >> 40 ) );
        EXPECT_EQ( data[15], static_cast<uint8_t>( TIMEOUT_MS >> 32 ) );
        EXPECT_EQ( data[16], static_cast<uint8_t>( TIMEOUT_MS >> 24 ) );
        EXPECT_EQ( data[17], static_cast<uint8_t>( TIMEOUT_MS >> 16 ) );
        EXPECT_EQ( data[18], static_cast<uint8_t>( TIMEOUT_MS >> 8 ) );
        EXPECT_EQ( data[19], static_cast<uint8_t>( TIMEOUT_MS ) );
        sendMessage( mConfig[actuatorName].canResponseId,
                     { 'A', 'B', 'C', 0x00, 0x01, 0x11, 0x22, 0x33, 0x44, 'c', 'a', 't', 0x00 } );
        ASSERT_EQ( std::future_status::ready,
                   callbackPromise.get_future().wait_for( std::chrono::milliseconds( TIMEOUT_MS ) ) );
    }

    void
    testTimeout( std::function<void()> sendMessageCallback )
    {
        CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
        ASSERT_TRUE( dispatcher.init() );
        MockNotifyCommandStatusCallback callback1;
        std::promise<void> callbackPromise;
        EXPECT_CALL( callback1, Call( _, _, _ ) )
            .Times( 1 )
            .WillOnce( Invoke( [&callbackPromise]( CommandStatus status,
                                                   CommandReasonCode reasonCode,
                                                   const CommandReasonDescription &reasonDescription ) {
                EXPECT_EQ( status, CommandStatus::EXECUTION_TIMEOUT );
                EXPECT_EQ( reasonCode, REASON_CODE_NO_RESPONSE );
                static_cast<void>( reasonDescription );
                callbackPromise.set_value();
            } ) );
        auto actuatorName = "Vehicle.actuator6";
        SignalValueWrapper value;
        std::vector<uint8_t> data;
        value.value = static_cast<int32_t>( 0xAABBCCDD );
        value.type = SignalType::INT32;
        auto issuedTimestamp = ClockHandler::getClock()->systemTimeSinceEpochMs();
        dispatcher.setActuatorValue(
            actuatorName, value, "ABC", issuedTimestamp, TIMEOUT_MS, callback1.AsStdFunction() );
        uint32_t id;
        receiveMessage( id, data );
        EXPECT_EQ( id, mConfig[actuatorName].canRequestId );
        ASSERT_GE( data.size(), 20 );
        EXPECT_EQ( data[0], 'A' );
        EXPECT_EQ( data[1], 'B' );
        EXPECT_EQ( data[2], 'C' );
        EXPECT_EQ( data[3], 0x00 );
        EXPECT_EQ( data[4], static_cast<uint8_t>( issuedTimestamp >> 56 ) );
        EXPECT_EQ( data[5], static_cast<uint8_t>( issuedTimestamp >> 48 ) );
        EXPECT_EQ( data[6], static_cast<uint8_t>( issuedTimestamp >> 40 ) );
        EXPECT_EQ( data[7], static_cast<uint8_t>( issuedTimestamp >> 32 ) );
        EXPECT_EQ( data[8], static_cast<uint8_t>( issuedTimestamp >> 24 ) );
        EXPECT_EQ( data[9], static_cast<uint8_t>( issuedTimestamp >> 16 ) );
        EXPECT_EQ( data[10], static_cast<uint8_t>( issuedTimestamp >> 8 ) );
        EXPECT_EQ( data[11], static_cast<uint8_t>( issuedTimestamp ) );
        EXPECT_EQ( data[12], static_cast<uint8_t>( TIMEOUT_MS >> 56 ) );
        EXPECT_EQ( data[13], static_cast<uint8_t>( TIMEOUT_MS >> 48 ) );
        EXPECT_EQ( data[14], static_cast<uint8_t>( TIMEOUT_MS >> 40 ) );
        EXPECT_EQ( data[15], static_cast<uint8_t>( TIMEOUT_MS >> 32 ) );
        EXPECT_EQ( data[16], static_cast<uint8_t>( TIMEOUT_MS >> 24 ) );
        EXPECT_EQ( data[17], static_cast<uint8_t>( TIMEOUT_MS >> 16 ) );
        EXPECT_EQ( data[18], static_cast<uint8_t>( TIMEOUT_MS >> 8 ) );
        EXPECT_EQ( data[19], static_cast<uint8_t>( TIMEOUT_MS ) );
        sendMessageCallback();
        ASSERT_EQ( std::future_status::ready,
                   callbackPromise.get_future().wait_for( std::chrono::milliseconds( 2 * TIMEOUT_MS ) ) );
    }
};

TEST_F( CanCommandDispatcherTest, invalidCanInterfaceName )
{
    CanCommandDispatcher dispatcher( mConfig, "abc", &mRawDataBufferManagerSpy );
    ASSERT_FALSE( dispatcher.init() );
}

TEST_F( CanCommandDispatcherTest, getActuatorNames )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    auto names = dispatcher.getActuatorNames();
    ASSERT_EQ( names.size(), 13 );
}

TEST_F( CanCommandDispatcherTest, notSupportedActuator )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_NOT_SUPPORTED, _ ) ).Times( 1 );
    dispatcher.setActuatorValue( "Vehicle.actuator99",
                                 SignalValueWrapper{},
                                 "CMD123",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, wrongArgumentType )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_ARGUMENT_TYPE_MISMATCH, _ ) ).Times( 1 );
    SignalValueWrapper value;
    value.value = 1.0;
    value.type = SignalType::DOUBLE;
    dispatcher.setActuatorValue( "Vehicle.actuator6",
                                 value,
                                 "CMD123",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, unsupportedArgumentType )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, _ ) ).Times( 1 );
    SignalValueWrapper value;
    value.type = static_cast<SignalType>( -1 );
    dispatcher.setActuatorValue( "Vehicle.actuator13",
                                 value,
                                 "CMD123",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, commandIdTooLong )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, _ ) ).Times( 1 );
    SignalValueWrapper value;
    value.value = static_cast<int32_t>( 0xAABBCCDD );
    value.type = SignalType::INT32;
    dispatcher.setActuatorValue( "Vehicle.actuator6",
                                 value,
                                 "0123456789012345678901234567890123456789012345678901234567890123456789",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, timedOutBeforeDispatch )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_TIMEOUT, REASON_CODE_TIMED_OUT_BEFORE_DISPATCH, _ ) )
        .Times( 1 );
    SignalValueWrapper value;
    value.value = static_cast<int32_t>( 0xAABBCCDD );
    value.type = SignalType::INT32;
    dispatcher.setActuatorValue( "Vehicle.actuator6",
                                 value,
                                 "CMD123",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs() - 1000,
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, stringBadBorrow )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, _ ) ).Times( 1 );
    SignalValueWrapper value;
    value.value.rawDataVal.signalId = 999;
    value.value.rawDataVal.handle = 1234;
    value.type = SignalType::STRING;
    dispatcher.setActuatorValue( "Vehicle.actuator12",
                                 value,
                                 "CMD123",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, argumentTooLong )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback;
    EXPECT_CALL( callback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, _ ) ).Times( 1 );
    SignalValueWrapper value;
    value.value = static_cast<int32_t>( 0xAABBCCDD );
    value.type = SignalType::INT32;
    dispatcher.setActuatorValue( "Vehicle.actuator6",
                                 value,
                                 "012345678901234567890123456789012345678901234567890123456789",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback.AsStdFunction() );
}

TEST_F( CanCommandDispatcherTest, successfulAllTypes )
{
    SignalValueWrapper value;
    std::vector<uint8_t> data;

    // UINT8
    value.value = static_cast<uint8_t>( 0xAA );
    value.type = SignalType::UINT8;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator1", value, data ) );
    ASSERT_EQ( data.size(), 21 );
    EXPECT_EQ( data[20], 0xAA );

    // INT8
    value.value = static_cast<int8_t>( 0xAA );
    value.type = SignalType::INT8;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator2", value, data ) );
    ASSERT_EQ( data.size(), 21 );
    EXPECT_EQ( data[20], 0xAA );

    // UINT16
    value.value = static_cast<uint16_t>( 0xAABB );
    value.type = SignalType::UINT16;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator3", value, data ) );
    ASSERT_EQ( data.size(), 22 );
    EXPECT_EQ( data[20], 0xAA );
    EXPECT_EQ( data[21], 0xBB );

    // INT16
    value.value = static_cast<int16_t>( 0xAABB );
    value.type = SignalType::INT16;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator4", value, data ) );
    ASSERT_EQ( data.size(), 22 );
    EXPECT_EQ( data[20], 0xAA );
    EXPECT_EQ( data[21], 0xBB );

    // UINT32
    value.value = static_cast<uint32_t>( 0xAABBCCDD );
    value.type = SignalType::UINT32;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator5", value, data ) );
    ASSERT_EQ( data.size(), 24 );
    EXPECT_EQ( data[20], 0xAA );
    EXPECT_EQ( data[21], 0xBB );
    EXPECT_EQ( data[22], 0xCC );
    EXPECT_EQ( data[23], 0xDD );

    // INT32
    value.value = static_cast<int32_t>( 0xAABBCCDD );
    value.type = SignalType::INT32;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator6", value, data ) );
    ASSERT_EQ( data.size(), 24 );
    EXPECT_EQ( data[20], 0xAA );
    EXPECT_EQ( data[21], 0xBB );
    EXPECT_EQ( data[22], 0xCC );
    EXPECT_EQ( data[23], 0xDD );

    // UINT64
    value.value = static_cast<uint64_t>( 0xAABBCCDD00112233 );
    value.type = SignalType::UINT64;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator7", value, data ) );
    ASSERT_EQ( data.size(), 28 );
    EXPECT_EQ( data[20], 0xAA );
    EXPECT_EQ( data[21], 0xBB );
    EXPECT_EQ( data[22], 0xCC );
    EXPECT_EQ( data[23], 0xDD );
    EXPECT_EQ( data[24], 0x00 );
    EXPECT_EQ( data[25], 0x11 );
    EXPECT_EQ( data[26], 0x22 );
    EXPECT_EQ( data[27], 0x33 );

    // INT64
    value.value = static_cast<int64_t>( 0xAABBCCDD00112233 );
    value.type = SignalType::INT64;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator8", value, data ) );
    ASSERT_EQ( data.size(), 28 );
    EXPECT_EQ( data[20], 0xAA );
    EXPECT_EQ( data[21], 0xBB );
    EXPECT_EQ( data[22], 0xCC );
    EXPECT_EQ( data[23], 0xDD );
    EXPECT_EQ( data[24], 0x00 );
    EXPECT_EQ( data[25], 0x11 );
    EXPECT_EQ( data[26], 0x22 );
    EXPECT_EQ( data[27], 0x33 );

    // FLOAT
    value.value = static_cast<float>( 123.0 );
    value.type = SignalType::FLOAT;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator9", value, data ) );
    ASSERT_EQ( data.size(), 24 );
    EXPECT_EQ( data[20], 0x42 );
    EXPECT_EQ( data[21], 0xF6 );
    EXPECT_EQ( data[22], 0x00 );
    EXPECT_EQ( data[23], 0x00 );

    // DOUBLE
    value.value = static_cast<double>( 456.0 );
    value.type = SignalType::DOUBLE;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator10", value, data ) );
    ASSERT_EQ( data.size(), 28 );
    EXPECT_EQ( data[20], 0x40 );
    EXPECT_EQ( data[21], 0x7C );
    EXPECT_EQ( data[22], 0x80 );
    EXPECT_EQ( data[23], 0x00 );
    EXPECT_EQ( data[24], 0x00 );
    EXPECT_EQ( data[25], 0x0 );
    EXPECT_EQ( data[26], 0x00 );
    EXPECT_EQ( data[27], 0x00 );

    // BOOL
    value.value = true;
    value.type = SignalType::BOOLEAN;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator11", value, data ) );
    ASSERT_EQ( data.size(), 21 );
    EXPECT_EQ( data[20], 0x01 );

    // STRING
    mRawDataBufferManagerSpy.updateConfig( { { 1, { 1, "", "" } } } );
    std::string stringVal = "dog";
    auto handle = mRawDataBufferManagerSpy.push(
        reinterpret_cast<const uint8_t *>( stringVal.data() ), stringVal.size(), 1234, 1 );
    mRawDataBufferManagerSpy.increaseHandleUsageHint( 1, handle, RawData::BufferHandleUsageStage::UPLOADING );
    value.value.rawDataVal.handle = handle;
    value.value.rawDataVal.signalId = 1;
    value.type = SignalType::STRING;
    ASSERT_NO_FATAL_FAILURE( testSuccessful( "Vehicle.actuator12", value, data ) );
    ASSERT_EQ( data.size(), 24 );
    EXPECT_EQ( data[20], 'd' );
    EXPECT_EQ( data[21], 'o' );
    EXPECT_EQ( data[22], 'g' );
    EXPECT_EQ( data[23], 0x00 );
}

TEST_F( CanCommandDispatcherTest, responseIgnoredNoCommandId )
{
    testTimeout( [this]() {
        sendMessage( mConfig["Vehicle.actuator6"].canResponseId, {} );
    } );
}

TEST_F( CanCommandDispatcherTest, responseIgnoredNoStatus )
{
    testTimeout( [this]() {
        sendMessage( mConfig["Vehicle.actuator6"].canResponseId, { 'A', 'B', 'C', 0x00 } );
    } );
}

TEST_F( CanCommandDispatcherTest, responseIgnoredNoReasonCode )
{
    testTimeout( [this]() {
        sendMessage( mConfig["Vehicle.actuator6"].canResponseId, { 'A', 'B', 'C', 0x00, 0x01 } );
    } );
}

TEST_F( CanCommandDispatcherTest, responseIgnoredNoReasonDescription )
{
    testTimeout( [this]() {
        sendMessage( mConfig["Vehicle.actuator6"].canResponseId,
                     { 'A', 'B', 'C', 0x00, 0x01, 0x11, 0x22, 0x33, 0x44 } );
    } );
}

TEST_F( CanCommandDispatcherTest, responseIgnoredWrongCommandId )
{
    testTimeout( [this]() {
        sendMessage( mConfig["Vehicle.actuator6"].canResponseId,
                     { 'X', 'Y', 'Z', 0x00, 0x01, 0x11, 0x22, 0x33, 0x44, 'c', 'a', 't', 0x00 } );
    } );
}

TEST_F( CanCommandDispatcherTest, responseIgnoredWrongResponseId )
{
    testTimeout( [this]() {
        sendMessage( mConfig["Vehicle.actuator5"].canResponseId,
                     { 'A', 'B', 'C', 0x00, 0x01, 0x11, 0x22, 0x33, 0x44, 'c', 'a', 't', 0x00 } );
    } );
}

TEST_F( CanCommandDispatcherTest, inProgressSuccess )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback1;
    std::promise<void> callbackPromise;
    EXPECT_CALL( callback1, Call( _, _, _ ) )
        .Times( 2 )
        .WillOnce( Invoke( []( CommandStatus status,
                               CommandReasonCode reasonCode,
                               const CommandReasonDescription &reasonDescription ) {
            EXPECT_EQ( status, CommandStatus::IN_PROGRESS );
            EXPECT_EQ( reasonCode, 0x11223344 );
            EXPECT_EQ( reasonDescription, "cat" );
        } ) )
        .WillOnce( Invoke( [&callbackPromise]( CommandStatus status,
                                               CommandReasonCode reasonCode,
                                               const CommandReasonDescription &reasonDescription ) {
            EXPECT_EQ( status, CommandStatus::SUCCEEDED );
            EXPECT_EQ( reasonCode, 0x55667788 );
            EXPECT_EQ( reasonDescription, "dog" );
            callbackPromise.set_value();
        } ) );
    auto actuatorName = "Vehicle.actuator6";
    SignalValueWrapper value;
    value.value = static_cast<int32_t>( 0xAA );
    value.type = SignalType::INT32;
    dispatcher.setActuatorValue( actuatorName,
                                 value,
                                 "ABC",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback1.AsStdFunction() );
    // Also attempt duplicate invocation with same command ID
    MockNotifyCommandStatusCallback callback2;
    EXPECT_CALL( callback2, Call( _, _, _ ) ).Times( 0 );
    dispatcher.setActuatorValue( actuatorName,
                                 value,
                                 "ABC",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback2.AsStdFunction() );
    uint32_t id;
    std::vector<uint8_t> data;
    receiveMessage( id, data );
    EXPECT_EQ( id, mConfig[actuatorName].canRequestId );
    ASSERT_GE( data.size(), 4 );
    EXPECT_EQ( data[0], 'A' );
    EXPECT_EQ( data[1], 'B' );
    EXPECT_EQ( data[2], 'C' );
    EXPECT_EQ( data[3], 0x00 );
    sendMessage( 0x123, {} ); // Send some other message to check it's ignored
    sendMessage( mConfig[actuatorName].canResponseId,
                 { 'A', 'B', 'C', 0x00, 0x0A, 0x11, 0x22, 0x33, 0x44, 'c', 'a', 't', 0x00 } );
    sendMessage( mConfig[actuatorName].canResponseId,
                 { 'A', 'B', 'C', 0x00, 0x01, 0x55, 0x66, 0x77, 0x88, 'd', 'o', 'g', 0x00 } );
    ASSERT_EQ( std::future_status::ready,
               callbackPromise.get_future().wait_for( std::chrono::milliseconds( TIMEOUT_MS ) ) );
}

TEST_F( CanCommandDispatcherTest, inProgressTimeout )
{
    CanCommandDispatcher dispatcher( mConfig, CAN_INTERFACE_NAME, &mRawDataBufferManagerSpy );
    ASSERT_TRUE( dispatcher.init() );
    MockNotifyCommandStatusCallback callback1;
    std::promise<void> callbackPromise;
    EXPECT_CALL( callback1, Call( _, _, _ ) )
        .Times( 2 )
        .WillOnce( Invoke( []( CommandStatus status,
                               CommandReasonCode reasonCode,
                               const CommandReasonDescription &reasonDescription ) {
            EXPECT_EQ( status, CommandStatus::IN_PROGRESS );
            EXPECT_EQ( reasonCode, 0x11223344 );
            EXPECT_EQ( reasonDescription, "cat" );
        } ) )
        .WillOnce( Invoke( [&callbackPromise]( CommandStatus status,
                                               CommandReasonCode reasonCode,
                                               const CommandReasonDescription &reasonDescription ) {
            EXPECT_EQ( status, CommandStatus::EXECUTION_TIMEOUT );
            EXPECT_EQ( reasonCode, REASON_CODE_NO_RESPONSE );
            static_cast<void>( reasonDescription );
            callbackPromise.set_value();
        } ) );
    auto actuatorName = "Vehicle.actuator6";
    SignalValueWrapper value;
    value.value = static_cast<int32_t>( 0xAA );
    value.type = SignalType::INT32;
    dispatcher.setActuatorValue( actuatorName,
                                 value,
                                 "ABC",
                                 ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                 TIMEOUT_MS,
                                 callback1.AsStdFunction() );
    uint32_t id;
    std::vector<uint8_t> data;
    receiveMessage( id, data );
    EXPECT_EQ( id, mConfig[actuatorName].canRequestId );
    ASSERT_GE( data.size(), 4 );
    EXPECT_EQ( data[0], 'A' );
    EXPECT_EQ( data[1], 'B' );
    EXPECT_EQ( data[2], 'C' );
    EXPECT_EQ( data[3], 0x00 );
    sendMessage( mConfig[actuatorName].canResponseId,
                 { 'A', 'B', 'C', 0x00, 0x0A, 0x11, 0x22, 0x33, 0x44, 'c', 'a', 't', 0x00 } );
    ASSERT_EQ( std::future_status::ready,
               callbackPromise.get_future().wait_for( std::chrono::milliseconds( 2 * TIMEOUT_MS ) ) );
}

} // namespace IoTFleetWise
} // namespace Aws
