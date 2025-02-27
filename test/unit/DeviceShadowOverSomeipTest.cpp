// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DeviceShadowOverSomeip.h"
#include "SenderMock.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "v1/commonapi/DeviceShadowOverSomeipInterface.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::MockFunction;
using ::testing::StrictMock;

class DeviceShadowOverSomeipTest : public ::testing::Test
{
protected:
    DeviceShadowOverSomeipTest()
        : mDeviceShadowOverSomeip( mSenderMock )
    {
    }
    void
    SetUp() override
    {
    }

    void
    TearDown() override
    {
    }

    StrictMock<Testing::SenderMock> mSenderMock;
    DeviceShadowOverSomeip mDeviceShadowOverSomeip;
};

TEST_F( DeviceShadowOverSomeipTest, updateInvalidSentJson )
{
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::INVALID_REQUEST ),
                       Eq( "JSON parse error" ),
                       _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "invalid", callback.AsStdFunction() );
}

TEST_F( DeviceShadowOverSomeipTest, updateConnectivityErrorInvalidRequest )
{
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce( InvokeArgument<3>( ConnectivityError::NotConfigured ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::INVALID_REQUEST ),
                       Eq( "NotConfigured" ),
                       _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "{}", callback.AsStdFunction() );
}

TEST_F( DeviceShadowOverSomeipTest, updateConnectivityErrorUnreachable )
{
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::SHADOW_SERVICE_UNREACHABLE ),
                       Eq( "TransmissionError" ),
                       _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "{}", callback.AsStdFunction() );
}

TEST_F( DeviceShadowOverSomeipTest, updateInvalidReceivedJson )
{
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback, Call( _, _, _ ) ).Times( 0 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "{}", callback.AsStdFunction() );
    {
        std::string payload = "invalid";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/update/accepted" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
}

TEST_F( DeviceShadowOverSomeipTest, updateOtherClient )
{
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback, Call( _, _, _ ) ).Times( 0 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "{}", callback.AsStdFunction() );
    {
        std::string payload = "{}";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/update/accepted" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
}

TEST_F( DeviceShadowOverSomeipTest, updateAcceptedClassic )
{
    std::string clientToken;
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce(
            Invoke( [&clientToken](
                        const std::string &topic, const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) {
                static_cast<void>( topic );
                std::string sendMessage( buf, buf + size );
                Json::Reader jsonReader;
                Json::Value sendJson;
                ASSERT_TRUE( jsonReader.parse( sendMessage, sendJson ) );
                clientToken = sendJson["clientToken"].asString();
                callback( ConnectivityError::Success );
            } ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR ), Eq( "" ), _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "{}", callback.AsStdFunction() );
    {
        std::string payload = "{\"clientToken\":\"" + clientToken + "\"}";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/update/accepted" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
}

TEST_F( DeviceShadowOverSomeipTest, updateRejectedNamed )
{
    std::string clientToken;
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/name/test/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce(
            Invoke( [&clientToken](
                        const std::string &topic, const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) {
                static_cast<void>( topic );
                std::string sendMessage( buf, buf + size );
                Json::Reader jsonReader;
                Json::Value sendJson;
                ASSERT_TRUE( jsonReader.parse( sendMessage, sendJson ) );
                clientToken = sendJson["clientToken"].asString();
                callback( ConnectivityError::Success );
            } ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::REJECTED ), Eq( "abc" ), _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "test", "{}", callback.AsStdFunction() );
    {
        // Check receiving own request is ignored
        std::string payload = "{\"clientToken\":\"" + clientToken + "\"}";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/update" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
    {
        std::string payload = "{\"clientToken\":\"" + clientToken + "\",\"message\":\"abc\"}";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/name/test/update/rejected" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
}

TEST_F( DeviceShadowOverSomeipTest, updateUnknown )
{
    std::string clientToken;
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/update", _, _, _ ) )
        .Times( 1 )
        .WillOnce(
            Invoke( [&clientToken](
                        const std::string &topic, const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) {
                static_cast<void>( topic );
                std::string sendMessage( buf, buf + size );
                Json::Reader jsonReader;
                Json::Value sendJson;
                ASSERT_TRUE( jsonReader.parse( sendMessage, sendJson ) );
                clientToken = sendJson["clientToken"].asString();
                callback( ConnectivityError::Success );
            } ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::UNKNOWN ), Eq( "" ), _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.updateShadow( nullptr, "", "{}", callback.AsStdFunction() );
    std::string payload = "{\"clientToken\":\"" + clientToken + "\"}";
    ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                         payload.size(),
                                         0,
                                         "$aws/things/thing-name/shadow/update/blah" );
    mDeviceShadowOverSomeip.onDataReceived( message );
}

TEST_F( DeviceShadowOverSomeipTest, documentsUpdateClassic )
{
    std::string payload = "{}";
    ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                         payload.size(),
                                         0,
                                         "$aws/things/thing-name/shadow/update/documents" );
    mDeviceShadowOverSomeip.onDataReceived( message );
}

TEST_F( DeviceShadowOverSomeipTest, documentsUpdateNamed )
{
    std::string payload = "{}";
    ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                         payload.size(),
                                         0,
                                         "$aws/things/thing-name/shadow/name/test/update/documents" );
    mDeviceShadowOverSomeip.onDataReceived( message );
}

TEST_F( DeviceShadowOverSomeipTest, documentsUpdateWrongPrefix )
{
    std::string payload = "{}";
    ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                         payload.size(),
                                         0,
                                         "wrong_$aws/things/thing-name/shadow/update/documents" );
    mDeviceShadowOverSomeip.onDataReceived( message );
}

TEST_F( DeviceShadowOverSomeipTest, getAccepted )
{
    std::string clientToken;
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/get", _, _, _ ) )
        .Times( 1 )
        .WillOnce(
            Invoke( [&clientToken](
                        const std::string &topic, const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) {
                static_cast<void>( topic );
                std::string sendMessage( buf, buf + size );
                Json::Reader jsonReader;
                Json::Value sendJson;
                ASSERT_TRUE( jsonReader.parse( sendMessage, sendJson ) );
                clientToken = sendJson["clientToken"].asString();
                callback( ConnectivityError::Success );
            } ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage,
                       const std::string &responseDocument )>
        callback;
    EXPECT_CALL( callback,
                 Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR ), Eq( "" ), _ ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.getShadow( nullptr, "", callback.AsStdFunction() );
    {
        std::string payload = "{\"clientToken\":\"" + clientToken + "\"}";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/get/accepted" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
}

TEST_F( DeviceShadowOverSomeipTest, deleteAccepted )
{
    std::string clientToken;
    EXPECT_CALL( mSenderMock, mockedSendBuffer( "$aws/things/thing-name/shadow/delete", _, _, _ ) )
        .Times( 1 )
        .WillOnce(
            Invoke( [&clientToken](
                        const std::string &topic, const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) {
                static_cast<void>( topic );
                std::string sendMessage( buf, buf + size );
                Json::Reader jsonReader;
                Json::Value sendJson;
                ASSERT_TRUE( jsonReader.parse( sendMessage, sendJson ) );
                clientToken = sendJson["clientToken"].asString();
                callback( ConnectivityError::Success );
            } ) );
    MockFunction<void( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
                       const std::string &errorMessage )>
        callback;
    EXPECT_CALL( callback, Call( Eq( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR ), Eq( "" ) ) )
        .Times( 1 );
    mDeviceShadowOverSomeip.deleteShadow( nullptr, "", callback.AsStdFunction() );
    {
        std::string payload = "{\"clientToken\":\"" + clientToken + "\"}";
        ReceivedConnectivityMessage message( reinterpret_cast<const uint8_t *>( payload.data() ),
                                             payload.size(),
                                             0,
                                             "$aws/things/thing-name/shadow/delete/accepted" );
        mDeviceShadowOverSomeip.onDataReceived( message );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
