// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/AwsGreengrassV2ConnectivityModule.h"
#include "AwsGreengrassCoreIpcClientWrapperMock.h"
#include "aws/iotfleetwise/AwsSDKMemoryManager.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include "gmock/gmock.h"
#include <atomic>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::A;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::StrictMock;

class AwsGreengrassV2ConnectivityModuleTest : public ::testing::Test
{
protected:
    AwsGreengrassV2ConnectivityModuleTest()
        : mTopicConfig( "thing-name", TopicConfigArgs() )
        , mConnectivityModule( mGreengrassClientWrapper, mTopicConfig )
    {
    }

    void
    SetUp() override
    {
        ON_CALL( mGreengrassClientWrapper, Connect( _, _ ) )
            .WillByDefault( Invoke( [this]( ConnectionLifecycleHandler &lifecycleHandler, const ConnectionConfig & ) {
                lifecycleHandler.OnConnectCallback();
                mConnectPromise.set_value( RpcError{ EVENT_STREAM_RPC_SUCCESS, 0 } );
                return mConnectPromise.get_future();
            } ) );

        mConnectivityModule.subscribeToConnectionEstablished( [this]() {
            mConnected = true;
        } );
    }

    void
    connect()
    {
        EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );

        ASSERT_TRUE( mConnectivityModule.connect() );
        ASSERT_TRUE( mConnectivityModule.isAlive() );
        ASSERT_TRUE( mConnected );
    }

    std::promise<RpcError> mConnectPromise;
    std::atomic_bool mConnected{ false };

    AwsGreengrassCoreIpcClientWrapperMock mGreengrassClientWrapper;
    TopicConfig mTopicConfig;
    AwsGreengrassV2ConnectivityModule mConnectivityModule;
};

TEST_F( AwsGreengrassV2ConnectivityModuleTest, connectSuccessfully )
{
    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule.connect() );
    ASSERT_TRUE( mConnectivityModule.isAlive() );
    ASSERT_TRUE( mConnected );

    ASSERT_TRUE( mConnectivityModule.disconnect() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, connectFailure )
{
    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) )
        .WillOnce( Invoke( [this]( ConnectionLifecycleHandler &, const ConnectionConfig & ) {
            mConnectPromise.set_value( RpcError{ EVENT_STREAM_RPC_CONNECTION_ACCESS_DENIED, 1 } );
            return mConnectPromise.get_future();
        } ) );

    ASSERT_FALSE( mConnectivityModule.connect() );
    ASSERT_FALSE( mConnectivityModule.isAlive() );
    ASSERT_FALSE( mConnected );

    ASSERT_TRUE( mConnectivityModule.disconnect() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, receiveMessage )
{
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver1;
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver2;

    auto receiver1 = mConnectivityModule.createReceiver( "topic1" );
    auto receiver2 = mConnectivityModule.createReceiver( "topic2" );

    receiver1->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );
    receiver2->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver2.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );
    mGreengrassClientWrapper.setSubscribeResponse( "topic1", std::make_unique<OperationResponse>() );
    mGreengrassClientWrapper.setSubscribeResponse( "topic2", std::make_unique<OperationResponse>() );

    ASSERT_TRUE( mConnectivityModule.connect() );
    ASSERT_TRUE( mGreengrassClientWrapper.isTopicSubscribed( "topic1" ) );
    ASSERT_TRUE( mGreengrassClientWrapper.isTopicSubscribed( "topic2" ) );

    auto publishTime = ClockHandler::getClock()->monotonicTimeSinceEpochMs();
    // Simulate messages coming from MQTT client
    mGreengrassClientWrapper.publishToSubscribedTopic( "topic1", "data1" );
    mGreengrassClientWrapper.publishToSubscribedTopic( "topic2", "data2" );

    ASSERT_EQ( receivedDataReceiver1.size(), 1 );
    ASSERT_EQ( receivedDataReceiver1[0].first, "data1" );
    ASSERT_GE( receivedDataReceiver1[0].second.receivedMonotonicTimeMs, publishTime );
    // Give some margin for error due to test being slow, but make sure that timeout is not much more
    // than expected.
    ASSERT_LE( receivedDataReceiver1[0].second.receivedMonotonicTimeMs, publishTime + 500 );

    ASSERT_EQ( receivedDataReceiver2.size(), 1 );
    ASSERT_EQ( receivedDataReceiver2[0].first, "data2" );

    ASSERT_TRUE( mConnectivityModule.disconnect() );
    ASSERT_FALSE( mGreengrassClientWrapper.isTopicSubscribed( "topic1" ) );
    ASSERT_FALSE( mGreengrassClientWrapper.isTopicSubscribed( "topic2" ) );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, subscribeToInvalidTopic )
{
    std::vector<std::pair<std::string, ReceivedConnectivityMessage>> receivedDataReceiver1;

    auto receiver1 = mConnectivityModule.createReceiver( "" );

    receiver1->subscribeToDataReceived( [&]( const ReceivedConnectivityMessage &message ) {
        receivedDataReceiver1.emplace_back( std::string( message.buf, message.buf + message.size ), message );
    } );

    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule.connect() );
    ASSERT_FALSE( mGreengrassClientWrapper.isTopicSubscribed( "" ) );

    // Simulate messages coming from MQTT client
    mGreengrassClientWrapper.publishToSubscribedTopic( "", "data1" );

    ASSERT_EQ( receivedDataReceiver1.size(), 0 );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, subscribeFailureOnOperationActivation )
{
    auto receiver1 = mConnectivityModule.createReceiver( "topic1" );

    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );
    {
        auto operationResponse = std::make_unique<OperationResponse>();
        operationResponse->activateResponse = RpcError{ EVENT_STREAM_RPC_CRT_ERROR, 1 };
        mGreengrassClientWrapper.setSubscribeResponse( "topic1", std::move( operationResponse ) );
    }

    ASSERT_TRUE( mConnectivityModule.connect() );
    ASSERT_FALSE( mGreengrassClientWrapper.isTopicSubscribed( "topic1" ) );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, subscribeFailureOnOperationResultRpc )
{
    auto receiver1 = mConnectivityModule.createReceiver( "topic1" );

    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );
    {
        auto operationResponse = std::make_unique<OperationResponse>();
        operationResponse->getResultResponse = RpcError{ EVENT_STREAM_RPC_CRT_ERROR, 1 };
        mGreengrassClientWrapper.setSubscribeResponse( "topic1", std::move( operationResponse ) );
    }

    ASSERT_TRUE( mConnectivityModule.connect() );
    ASSERT_FALSE( mGreengrassClientWrapper.isTopicSubscribed( "topic1" ) );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, subscribeFailureOnOperationResult )
{
    auto receiver1 = mConnectivityModule.createReceiver( "topic1" );

    EXPECT_CALL( mGreengrassClientWrapper, Connect( _, _ ) ).Times( 1 );
    {
        auto operationResponse = std::make_unique<OperationResponse>();
        auto operationError = std::make_shared<Greengrass::InvalidArgumentsError>();
        operationError->SetMessage( "Argument1 is missing" );
        operationResponse->getResultResponse = std::shared_ptr<OperationError>( operationError );
        mGreengrassClientWrapper.setSubscribeResponse( "topic1", std::move( operationResponse ) );
    }

    ASSERT_TRUE( mConnectivityModule.connect() );
    ASSERT_FALSE( mGreengrassClientWrapper.isTopicSubscribed( "topic1" ) );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendWithoutConnection )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_FALSE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NoConnection ) ).Times( 1 );
    uint8_t input[] = { 0xca, 0xfe };
    sender->sendBuffer(
        sender->getTopicConfig().telemetryDataTopic, input, sizeof( input ), resultCallback.AsStdFunction() );

    ASSERT_FALSE( sender->isAlive() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendAfterDisconnection )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    ASSERT_TRUE( mConnectivityModule.disconnect() );
    ASSERT_FALSE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NoConnection ) ).Times( 1 );
    uint8_t input[] = { 0xca, 0xfe };
    sender->sendBuffer(
        sender->getTopicConfig().telemetryDataTopic, input, sizeof( input ), resultCallback.AsStdFunction() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendWithInvalidTopic )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NotConfigured ) ).Times( 1 );
    uint8_t input[] = { 0xca, 0xfe };
    sender->sendBuffer( "", input, sizeof( input ), resultCallback.AsStdFunction() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendWithInvalidData )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::WrongInputData ) ).Times( 1 );
    sender->sendBuffer( sender->getTopicConfig().telemetryDataTopic, nullptr, 0, resultCallback.AsStdFunction() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendPayloadTooLarge )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::WrongInputData ) ).Times( 1 );
    size_t maxMessageSizeBytes = 128 * 1024;
    std::vector<uint8_t> input( maxMessageSizeBytes + 1, 0xca );
    sender->sendBuffer(
        sender->getTopicConfig().telemetryDataTopic, input.data(), input.size(), resultCallback.AsStdFunction() );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendPayload )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::Success ) ).Times( 1 );
    mGreengrassClientWrapper.setPublishResponse( sender->getTopicConfig().telemetryDataTopic,
                                                 std::make_unique<OperationResponse>() );

    std::string input = "data1";
    sender->sendBuffer( sender->getTopicConfig().telemetryDataTopic,
                        reinterpret_cast<const uint8_t *>( input.data() ),
                        input.size(),
                        resultCallback.AsStdFunction() );

    auto topicNameToPublishedData = mGreengrassClientWrapper.getPublishedData();
    ASSERT_EQ( topicNameToPublishedData.size(), 1 );
    auto dataSentToTopic = topicNameToPublishedData.find( sender->getTopicConfig().telemetryDataTopic );
    ASSERT_EQ( dataSentToTopic->second.size(), 1 );
    ASSERT_EQ( dataSentToTopic->second[0], "data1" );
    ASSERT_EQ( sender->getPayloadCountSent(), 1 );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendPayloadFailureOnOperationActivation )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NoConnection ) ).Times( 1 );
    {
        auto operationResponse = std::make_unique<OperationResponse>();
        operationResponse->activateResponse = RpcError{ EVENT_STREAM_RPC_CRT_ERROR, 1 };
        mGreengrassClientWrapper.setPublishResponse( sender->getTopicConfig().telemetryDataTopic,
                                                     std::move( operationResponse ) );
    }

    std::string input = "data1";
    sender->sendBuffer( sender->getTopicConfig().telemetryDataTopic,
                        reinterpret_cast<const uint8_t *>( input.data() ),
                        input.size(),
                        resultCallback.AsStdFunction() );

    ASSERT_EQ( mGreengrassClientWrapper.getPublishedData().size(), 0 );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendPayloadFailureOnOperationResultRpc )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::TransmissionError ) ).Times( 1 );
    {
        auto operationResponse = std::make_unique<OperationResponse>();
        operationResponse->getResultResponse = RpcError{ EVENT_STREAM_RPC_CRT_ERROR, 2 };
        mGreengrassClientWrapper.setPublishResponse( sender->getTopicConfig().telemetryDataTopic,
                                                     std::move( operationResponse ) );
    }

    std::string input = "data1";
    sender->sendBuffer( sender->getTopicConfig().telemetryDataTopic,
                        reinterpret_cast<const uint8_t *>( input.data() ),
                        input.size(),
                        resultCallback.AsStdFunction() );

    ASSERT_EQ( mGreengrassClientWrapper.getPublishedData().size(), 0 );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendPayloadFailureOnOperationResult )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::TransmissionError ) ).Times( 1 );
    {
        auto operationResponse = std::make_unique<OperationResponse>();
        auto operationError = std::make_shared<Greengrass::InvalidArgumentsError>();
        operationError->SetMessage( "Argument1 is missing" );
        operationResponse->getResultResponse = std::shared_ptr<OperationError>( operationError );
        FWE_LOG_ERROR( std::string( operationError->GetMessage()->c_str() ) );
        mGreengrassClientWrapper.setPublishResponse( sender->getTopicConfig().telemetryDataTopic,
                                                     std::move( operationResponse ) );
    }

    std::string input = "data1";
    sender->sendBuffer( sender->getTopicConfig().telemetryDataTopic,
                        reinterpret_cast<const uint8_t *>( input.data() ),
                        input.size(),
                        resultCallback.AsStdFunction() );

    ASSERT_EQ( mGreengrassClientWrapper.getPublishedData().size(), 0 );
}

TEST_F( AwsGreengrassV2ConnectivityModuleTest, sendPayloadFailureWhenSdkMemoryExceeded )
{
    auto sender = mConnectivityModule.createSender();
    ASSERT_NO_FATAL_FAILURE( connect() );
    ASSERT_TRUE( sender->isAlive() );

    MockFunction<void( ConnectivityError result )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::QuotaReached ) ).Times( 1 );
    mGreengrassClientWrapper.setPublishResponse( sender->getTopicConfig().telemetryDataTopic,
                                                 std::make_unique<OperationResponse>() );
    auto &memoryManager = AwsSDKMemoryManager::getInstance();
    auto reservedMemory = memoryManager.getLimit();
    ASSERT_TRUE( memoryManager.reserveMemory( reservedMemory ) );

    std::string input = "data1";
    sender->sendBuffer( sender->getTopicConfig().telemetryDataTopic,
                        reinterpret_cast<const uint8_t *>( input.data() ),
                        input.size(),
                        resultCallback.AsStdFunction() );

    ASSERT_EQ( memoryManager.releaseReservedMemory( reservedMemory ), 0 );

    ASSERT_EQ( mGreengrassClientWrapper.getPublishedData().size(), 0 );
}

} // namespace IoTFleetWise
} // namespace Aws
