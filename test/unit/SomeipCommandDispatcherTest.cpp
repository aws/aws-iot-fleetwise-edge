// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/SomeipCommandDispatcher.h"
#include "CommonAPIProxyMock.h"
#include "ExampleSomeipInterfaceProxyMock.h"
#include "RawDataBufferManagerSpy.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ExampleSomeipInterfaceWrapper.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/ISomeipInterfaceWrapper.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "v1/commonapi/ExampleSomeipInterfaceProxy.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <functional>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::An;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

class SomeipCommandDispatcherTest : public ::testing::Test
{
protected:
    SomeipCommandDispatcherTest()
        : mCommonAPIProxy( std::make_shared<StrictMock<CommonAPIProxyMock>>() )
        , mProxy( std::make_shared<StrictMock<ExampleSomeipInterfaceProxyMock<>>>( mCommonAPIProxy ) )
        , mRawDataBufferManagerSpy( RawData::BufferManagerConfig::create().get() )
        , mLRCEvent( std::make_shared<NiceMock<CommonAPIEventMock<std::string, int32_t, int32_t, std::string>>>() )
        , mProxyStatusEventMock( std::make_shared<NiceMock<CommonAPIEventMock<CommonAPI::AvailabilityStatus>>>() )
        , mExampleSomeipInterfaceWrapper( std::make_shared<ExampleSomeipInterfaceWrapper>(
              "",
              "",
              "",
              [this]( std::string, std::string, std::string ) {
                  return mProxy;
              },
              &mRawDataBufferManagerSpy,
              true ) )
        , mCommandDispatcher( std::make_shared<SomeipCommandDispatcher>( mExampleSomeipInterfaceWrapper ) )
    {
    }

    void
    SetUp() override
    {
        ON_CALL( *mProxy, getNotifyLRCStatusEvent() ).WillByDefault( ReturnRef( *mLRCEvent ) );
        EXPECT_CALL( *mProxy, getNotifyLRCStatusEvent() ).Times( testing::AnyNumber() );
        ON_CALL( *mProxy, getProxyStatusEvent() ).WillByDefault( ReturnRef( *mProxyStatusEventMock ) );
        EXPECT_CALL( *mProxy, getProxyStatusEvent() ).Times( testing::AnyNumber() );
        ON_CALL( *mProxyStatusEventMock, onFirstListenerAdded( _ ) )
            .WillByDefault( Invoke( [this]( std::function<void( const CommonAPI::AvailabilityStatus &val )> listener ) {
                listener( CommonAPI::AvailabilityStatus::AVAILABLE );
            } ) );
    }

    std::shared_ptr<StrictMock<CommonAPIProxyMock>> mCommonAPIProxy;
    std::shared_ptr<StrictMock<ExampleSomeipInterfaceProxyMock<>>> mProxy;
    NiceMock<Testing::RawDataBufferManagerSpy> mRawDataBufferManagerSpy;
    std::shared_ptr<NiceMock<CommonAPIEventMock<std::string, int32_t, int32_t, std::string>>> mLRCEvent;
    std::shared_ptr<NiceMock<CommonAPIEventMock<CommonAPI::AvailabilityStatus>>> mProxyStatusEventMock;
    std::shared_ptr<ExampleSomeipInterfaceWrapper> mExampleSomeipInterfaceWrapper;
    std::shared_ptr<SomeipCommandDispatcher> mCommandDispatcher;
};

TEST_F( SomeipCommandDispatcherTest, getActuatorNames )
{
    EXPECT_CALL( *mProxyStatusEventMock, onFirstListenerAdded( _ ) ).Times( testing::AnyNumber() );

    ASSERT_TRUE( mCommandDispatcher->init() );
    auto names = mCommandDispatcher->getActuatorNames();
    ASSERT_EQ( names.size(), mExampleSomeipInterfaceWrapper->getSupportedActuatorInfo().size() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInitSuccessful )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInitUnsuccessful )
{
    // Mock the BuildProxy function returns nullptr
    auto badExampleSomeipInterfaceWrapper = std::make_shared<ExampleSomeipInterfaceWrapper>(
        "",
        "",
        "",
        [this]( std::string, std::string, std::string ) {
            return nullptr;
        },
        nullptr,
        false );
    auto commandDispatcher = std::make_shared<SomeipCommandDispatcher>( badExampleSomeipInterfaceWrapper );
    ASSERT_FALSE( commandDispatcher->init() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherWithoutInitShallFail )
{
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_INTERNAL_ERROR, "Null proxy" ) )
        .Times( 1 );
    mCommandDispatcher->setActuatorValue(
        "Vehicle.actuator1", SignalValueWrapper(), "CmdId123", 0, 0, resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulInt32 )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setInt32Async( 1, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<int32_t>( 1, SignalType::INT32 );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator1",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulInt64 )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setInt64Async( 1, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<int64_t>( 1, SignalType::INT64 );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator2",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulBoolean )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setBooleanAsync( true, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<bool>( true, SignalType::BOOLEAN );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator3",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulFloat )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setFloatAsync( 1.0, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<float>( 1.0, SignalType::FLOAT );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator4",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulDouble )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setDoubleAsync( 1.0, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<double>( 1.0, SignalType::DOUBLE );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator5",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulInt32LRC )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setInt32LongRunningAsync( _, 1, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const std::string &_commandId,
                               const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _commandId );
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<int32_t>( 1, SignalType::INT32 );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator20",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandSuccessfulString )
{
    mRawDataBufferManagerSpy.updateConfig( { { 1, { 1, "", "" } } } );
    std::string stringVal = "dog";
    auto handle = mRawDataBufferManagerSpy.push(
        reinterpret_cast<const uint8_t *>( stringVal.data() ), stringVal.size(), 1234, 1 );
    mRawDataBufferManagerSpy.increaseHandleUsageHint( 1, handle, RawData::BufferHandleUsageStage::UPLOADING );
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setStringAsync( "dog", _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const std::string &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetStringAsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::SUCCESS );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::SUCCESS );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.value.rawDataVal.handle = handle;
    signal.value.rawDataVal.signalId = 1;
    signal.type = SignalType::STRING;
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::SUCCEEDED, REASON_CODE_OEM_RANGE_START, "SUCCESS" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator9",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandStringBadSignalId )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    SignalValueWrapper signal;
    signal.value.rawDataVal.handle = 2;
    signal.value.rawDataVal.signalId = 1;
    signal.type = SignalType::STRING;
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, "" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue( "Vehicle.actuator9",
                                          signal,
                                          "CmdId123",
                                          ClockHandler::getClock()->systemTimeSinceEpochMs(),
                                          0,
                                          resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandWithMismatchedValueType )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setInt32Async( _, _, _ ) ).Times( 0 );
    SignalValueWrapper signal;
    signal.setVal<double>( 1, SignalType::DOUBLE );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_ARGUMENT_TYPE_MISMATCH, "" ) )
        .Times( 1 );
    mCommandDispatcher->setActuatorValue(
        "Vehicle.actuator1", signal, "CmdId123", 0, 0, resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeCommandFailed )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setInt32Async( 1, _, _ ) )
        .Times( 1 )
        .WillOnce( Invoke( []( const int32_t &_value,
                               v1::commonapi::ExampleSomeipInterfaceProxy<>::SetInt32AsyncCallback _callback,
                               const CommonAPI::CallInfo *_info ) -> std::future<CommonAPI::CallStatus> {
            static_cast<void>( _value );
            static_cast<void>( _info );
            _callback( CommonAPI::CallStatus::CONNECTION_FAILED );
            std::promise<CommonAPI::CallStatus> result;
            result.set_value( CommonAPI::CallStatus::CONNECTION_FAILED );
            return result.get_future();
        } ) );
    SignalValueWrapper signal;
    signal.setVal<int32_t>( 1, SignalType::INT32 );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL(
        resultCallback,
        Call( CommandStatus::EXECUTION_FAILED,
              REASON_CODE_OEM_RANGE_START + static_cast<CommandReasonCode>( CommonAPI::CallStatus::CONNECTION_FAILED ),
              "CONNECTION_FAILED" ) )
        .Times( 1 );
    mCommandDispatcher->setActuatorValue(
        "Vehicle.actuator1", signal, "CmdId123", 0, 0, resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherInvokeNotSupportedCommand )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( true ) );
    EXPECT_CALL( *mProxy, setInt32Async( _, _, _ ) ).Times( 0 );
    SignalValueWrapper signal;
    signal.setVal<int32_t>( 1, SignalType::INT32 );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_NOT_SUPPORTED, "" ) ).Times( 1 );
    mCommandDispatcher->setActuatorValue(
        "Vehicle.NotSupportedActuator", signal, "CmdId123", 0, 0, resultCallback.AsStdFunction() );
}

TEST_F( SomeipCommandDispatcherTest, dispatcherFailedWithProxyUnavailable )
{
    ASSERT_TRUE( mCommandDispatcher->init() );
    EXPECT_CALL( *mProxy, isAvailable() ).Times( 1 ).WillOnce( Return( false ) );
    EXPECT_CALL( *mProxy, setInt32Async( _, _, _ ) ).Times( 0 );
    SignalValueWrapper signal;
    signal.setVal<int32_t>( 1, SignalType::INT32 );
    MockFunction<void(
        CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>
        resultCallback;
    EXPECT_CALL( resultCallback, Call( CommandStatus::EXECUTION_FAILED, REASON_CODE_UNAVAILABLE, "Proxy unavailable" ) )
        .Times( 1 );
    mCommandDispatcher->setActuatorValue(
        "Vehicle.actuator1", signal, "CmdId123", 0, 0, resultCallback.AsStdFunction() );
}

} // namespace IoTFleetWise
} // namespace Aws
