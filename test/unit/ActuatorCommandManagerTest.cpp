// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ActuatorCommandManager.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CommandDispatcherMock.h"
#include "CommandTypes.h"
#include "DataSenderTypes.h"
#include "ICommandDispatcher.h"
#include "IDecoderManifest.h"
#include "QueueTypes.h"
#include "RawDataBufferManagerSpy.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "WaitUntil.h"
#include <atomic>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <functional>
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
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::StrictMock;

constexpr uint32_t maxConcurrentCommandRequests = 3;

class ActuatorCommandManagerTest : public ::testing::Test
{
public:
    ActuatorCommandManagerTest()
        : mRawBufferManagerSpy( std::make_shared<NiceMock<Testing::RawDataBufferManagerSpy>>(
              RawData::BufferManagerConfig::create().get() ) )
    {
    }
    void
    SetUp() override
    {
        mCommandResponses = std::make_shared<DataSenderQueue>( 100, "Command Responses" );
        mCommandDispatcher = std::make_shared<StrictMock<Testing::CommandDispatcherMock>>();

        mActuatorCommandManager = std::make_unique<ActuatorCommandManager>(
            mCommandResponses, maxConcurrentCommandRequests, mRawBufferManagerSpy );

        mCommandResponses->subscribeToNewDataAvailable( [&]() {
            mReadyToPublishCallbackCount++;
        } );

        SignalIDToCustomSignalDecoderFormatMap signalIDToCustomSignalDecoderFormatMap = {
            { 1, CustomSignalDecoderFormat{ "30", "custom-decoder-0" } },
            { 2, CustomSignalDecoderFormat{ "30", "custom-decoder-1" } },
            { 10, CustomSignalDecoderFormat{ "31", "custom-decoder-10" } },
        };
        ASSERT_TRUE( mActuatorCommandManager->registerDispatcher( "30", mCommandDispatcher ) );

        mSignalIDToCustomSignalDecoderFormatMap =
            std::make_shared<const SignalIDToCustomSignalDecoderFormatMap>( signalIDToCustomSignalDecoderFormatMap );

        mDecoderManifestID = "dm1";

        EXPECT_CALL( *mCommandDispatcher, init() ).WillRepeatedly( Return( true ) );
    }

    void
    TearDown() override
    {
        mActuatorCommandManager->stop();
    }

    bool
    popCommandResponse( std::shared_ptr<const CommandResponse> &commandResponse )
    {
        std::shared_ptr<const DataToSend> senderData;
        auto succeeded = mCommandResponses->pop( senderData );
        commandResponse = std::dynamic_pointer_cast<const CommandResponse>( senderData );
        return succeeded;
    }

protected:
    std::shared_ptr<NiceMock<Testing::RawDataBufferManagerSpy>> mRawBufferManagerSpy;
    std::unique_ptr<ActuatorCommandManager> mActuatorCommandManager;
    std::shared_ptr<DataSenderQueue> mCommandResponses;
    std::shared_ptr<StrictMock<Testing::CommandDispatcherMock>> mCommandDispatcher;
    std::atomic<unsigned int> mReadyToPublishCallbackCount{ 0 };
    std::shared_ptr<const SignalIDToCustomSignalDecoderFormatMap> mSignalIDToCustomSignalDecoderFormatMap;
    SyncID mDecoderManifestID;
};

TEST_F( ActuatorCommandManagerTest, DuplicateInterfaceId )
{
    ASSERT_FALSE( mActuatorCommandManager->registerDispatcher( "30", mCommandDispatcher ) );
}

TEST_F( ActuatorCommandManagerTest, getActuatorNames )
{
    EXPECT_CALL( *mCommandDispatcher, getActuatorNames() ).WillOnce( Return( std::vector<std::string>{ "abc" } ) );
    auto names = mActuatorCommandManager->getActuatorNames();
    ASSERT_EQ( names.size(), 1 );
    ASSERT_EQ( names["30"].size(), 1 );
    ASSERT_EQ( names["30"][0], "abc" );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandSuccess )
{
    EXPECT_CALL( *mCommandDispatcher, setActuatorValue( _, _, _, _, _, _ ) )
        .WillOnce( Invoke( []( const std::string &actuatorName,
                               const SignalValueWrapper &signalValue,
                               const CommandID &commandId,
                               Timestamp issuedTimestampMs,
                               Timestamp executionTimeoutMs,
                               NotifyCommandStatusCallback notifyStatusCallback ) {
            static_cast<void>( actuatorName );
            static_cast<void>( signalValue );
            static_cast<void>( commandId );
            static_cast<void>( issuedTimestampMs );
            static_cast<void>( executionTimeoutMs );
            notifyStatusCallback( CommandStatus::SUCCEEDED, 0x1234, "" );
        } ) );

    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "successful-command";
    commandRequest.decoderID = mDecoderManifestID;
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest.executionTimeoutMs = 10000;
    commandRequest.signalID = 1;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    // Validate only one command was successfully processed
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "successful-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, 0x1234 );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandNoDecoderManifest )
{
    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "no-decoder-manifest-command";
    commandRequest.decoderID = mDecoderManifestID;
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest.executionTimeoutMs = 10000;
    commandRequest.signalID = 1;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    // Validate only one command was successfully processed
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "no-decoder-manifest-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandDecoderManifestMismatch )
{
    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "decoder-manifest-out-of-sync-command";
    commandRequest.decoderID = "wrong-dm-id";
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest.executionTimeoutMs = 10000;
    commandRequest.signalID = 1;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    // Validate only one command was successfully processed
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "decoder-manifest-out-of-sync-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandTimeout )
{
    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "timed-out-command";
    commandRequest.decoderID = mDecoderManifestID;
    // Set the issued time to be in the past, so that the command will have already expired
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs() - 1000;
    commandRequest.executionTimeoutMs = 500;
    commandRequest.signalID = 2;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    // Validate only one command was processed with the status timeout
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "timed-out-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_TIMEOUT );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_TIMED_OUT_BEFORE_DISPATCH );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandNoCustomDecoders )
{
    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID, nullptr );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "no-custom-decoder-command";
    commandRequest.decoderID = mDecoderManifestID;
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest.executionTimeoutMs = 10000;
    commandRequest.signalID = 3;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "no-custom-decoder-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandNoCustomDecoder )
{
    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "no-custom-decoder-command";
    commandRequest.decoderID = mDecoderManifestID;
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest.executionTimeoutMs = 10000;
    commandRequest.signalID = 3;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    // Validate only one command was processed with the status timeout
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "no-custom-decoder-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_NO_DECODING_RULES_FOUND );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessCommandNoCommandDispatcherForInterface )
{
    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest;
    commandRequest.commandID = "no-dispatcher-command";
    commandRequest.decoderID = mDecoderManifestID;
    commandRequest.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest.executionTimeoutMs = 10000;
    commandRequest.signalID = 10;
    commandRequest.signalValueWrapper.value.doubleVal = 10.5;

    ASSERT_EQ( mReadyToPublishCallbackCount, 0 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest );

    // Validate only one command was processed with the status timeout
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "no-dispatcher-command" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_NO_COMMAND_DISPATCHER_FOUND );
    ASSERT_EQ( mReadyToPublishCallbackCount, 1 );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, ProcessMultipleCommands )
{
    EXPECT_CALL( *mCommandDispatcher, setActuatorValue( _, _, _, _, _, _ ) )
        .WillOnce( Invoke( []( const std::string &actuatorName,
                               const SignalValueWrapper &signalValue,
                               const CommandID &commandId,
                               Timestamp issuedTimestampMs,
                               Timestamp executionTimeoutMs,
                               NotifyCommandStatusCallback notifyStatusCallback ) {
            static_cast<void>( actuatorName );
            static_cast<void>( signalValue );
            static_cast<void>( commandId );
            static_cast<void>( issuedTimestampMs );
            static_cast<void>( executionTimeoutMs );
            notifyStatusCallback( CommandStatus::SUCCEEDED, 0x1234, "" );
        } ) )
        .WillOnce( Invoke( []( const std::string &actuatorName,
                               const SignalValueWrapper &signalValue,
                               const CommandID &commandId,
                               Timestamp issuedTimestampMs,
                               Timestamp executionTimeoutMs,
                               NotifyCommandStatusCallback notifyStatusCallback ) {
            static_cast<void>( actuatorName );
            static_cast<void>( signalValue );
            static_cast<void>( commandId );
            static_cast<void>( issuedTimestampMs );
            static_cast<void>( executionTimeoutMs );
            notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_PRECONDITION_FAILED, "" );
        } ) );

    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest1;
    commandRequest1.commandID = "command1";
    commandRequest1.decoderID = mDecoderManifestID;
    commandRequest1.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest1.executionTimeoutMs = 10000;
    commandRequest1.signalID = 1;
    commandRequest1.signalValueWrapper.value.doubleVal = 10.5;

    ActuatorCommandRequest commandRequest2;
    commandRequest2.commandID = "command2";
    commandRequest2.decoderID = mDecoderManifestID;
    commandRequest2.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest2.executionTimeoutMs = 10000;
    commandRequest2.signalID = 2;
    commandRequest2.signalValueWrapper.value.doubleVal = 20.5;

    ActuatorCommandRequest commandRequest3;
    commandRequest3.commandID = "command3";
    commandRequest3.decoderID = mDecoderManifestID;
    commandRequest3.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest3.executionTimeoutMs = 10000;
    commandRequest3.signalID = 3;
    commandRequest3.signalValueWrapper.value.doubleVal = 30.5;

    ActuatorCommandRequest commandRequest4;
    commandRequest4.commandID = "command4";
    commandRequest4.decoderID = mDecoderManifestID;
    commandRequest4.issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    commandRequest4.executionTimeoutMs = 10000;
    commandRequest4.signalID = 4;
    commandRequest4.signalValueWrapper.value.doubleVal = 40.5;

    mActuatorCommandManager->onReceivingCommandRequest( commandRequest1 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest2 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest3 );
    mActuatorCommandManager->onReceivingCommandRequest( commandRequest4 );

    // Validate only one command was successfully processed
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, 0x1234 );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command2" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_PRECONDITION_FAILED );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command3" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_NO_DECODING_RULES_FOUND );

    ASSERT_TRUE( mCommandResponses->isEmpty() );
}

TEST_F( ActuatorCommandManagerTest, NoCommandResponsesQueue )
{
    std::unique_ptr<ActuatorCommandManager> commandManager =
        std::make_unique<ActuatorCommandManager>( nullptr, maxConcurrentCommandRequests, mRawBufferManagerSpy );
    ASSERT_FALSE( commandManager->start() );
}

TEST_F( ActuatorCommandManagerTest, StringValue )
{
    auto currentTime = ClockHandler::getClock()->systemTimeSinceEpochMs();
    mRawBufferManagerSpy->updateConfig( { { 1, { 1, "", "" } } } );
    std::string stringVal = "hello";
    auto handle =
        mRawBufferManagerSpy->push( reinterpret_cast<const uint8_t *>( stringVal.data() ), stringVal.size(), 1234, 1 );
    mRawBufferManagerSpy->increaseHandleUsageHint( 1, handle, RawData::BufferHandleUsageStage::UPLOADING );

    EXPECT_CALL( *mCommandDispatcher, setActuatorValue( _, _, _, _, _, _ ) )
        .WillOnce( Invoke( [this, currentTime, handle, stringVal]( const std::string &actuatorName,
                                                                   const SignalValueWrapper &signalValue,
                                                                   const CommandID &commandId,
                                                                   Timestamp issuedTimestampMs,
                                                                   Timestamp executionTimeoutMs,
                                                                   NotifyCommandStatusCallback notifyStatusCallback ) {
            EXPECT_EQ( actuatorName, "custom-decoder-0" );
            EXPECT_EQ( signalValue.type, SignalType::STRING );
            EXPECT_EQ( commandId, "command1" );
            EXPECT_EQ( issuedTimestampMs, currentTime );
            EXPECT_EQ( executionTimeoutMs, 10000 );
            auto loanedFrame = mRawBufferManagerSpy->borrowFrame( signalValue.value.rawDataVal.signalId,
                                                                  signalValue.value.rawDataVal.handle );
            EXPECT_FALSE( loanedFrame.isNull() );
            if ( !loanedFrame.isNull() )
            {
                std::string receivedStringVal;
                receivedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ),
                                          loanedFrame.getSize() );
                EXPECT_EQ( receivedStringVal, stringVal );
            }
            notifyStatusCallback( CommandStatus::SUCCEEDED, 0x1234, "xyz" );
        } ) );

    mActuatorCommandManager->start();
    WAIT_ASSERT_TRUE( mActuatorCommandManager->isAlive() );

    mActuatorCommandManager->onChangeOfCustomSignalDecoderFormatMap( mDecoderManifestID,
                                                                     mSignalIDToCustomSignalDecoderFormatMap );

    ASSERT_TRUE( mCommandResponses->isEmpty() );

    ActuatorCommandRequest commandRequest1;
    commandRequest1.commandID = "command1";
    commandRequest1.decoderID = mDecoderManifestID;
    commandRequest1.issuedTimestampMs = currentTime;
    commandRequest1.executionTimeoutMs = 10000;
    commandRequest1.signalID = 1;
    commandRequest1.signalValueWrapper.type = SignalType::STRING;
    commandRequest1.signalValueWrapper.value.rawDataVal.signalId = 1;
    commandRequest1.signalValueWrapper.value.rawDataVal.handle = handle;

    mActuatorCommandManager->onReceivingCommandRequest( commandRequest1 );

    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );

    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, 0x1234 );
    ASSERT_EQ( commandResponse->reasonDescription, "xyz" );
}

} // namespace IoTFleetWise
} // namespace Aws
