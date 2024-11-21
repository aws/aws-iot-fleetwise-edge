// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandSchema.h"
#include "AwsIotConnectivityModule.h"
#include "AwsIotReceiver.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CommandTypes.h"
#include "DataSenderTypes.h"
#include "ICommandDispatcher.h"
#include "MqttClientWrapper.h"
#include "QueueTypes.h"
#include "RawDataBufferManagerSpy.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "TopicConfig.h"
#include "WaitUntil.h"
#include "command_request.pb.h"
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <boost/optional/optional.hpp>
#include <cfloat>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Gt;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

class CommandSchemaTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        TopicConfigArgs topicConfigArgs;
        mTopicConfig = std::make_unique<TopicConfig>( "thing-name", topicConfigArgs );
        mAwsIotModule = std::make_unique<AwsIotConnectivityModule>( "", "", nullptr, *mTopicConfig );

        std::shared_ptr<MqttClientWrapper> nullMqttClient;

        mReceiverCommandRequest = std::make_shared<AwsIotReceiver>( mAwsIotModule.get(), nullMqttClient, "topic" );

        mCommandResponses = std::make_shared<DataSenderQueue>( 100, "Command Responses" );

        mRawBufferManagerSpy = std::make_shared<NiceMock<Testing::RawDataBufferManagerSpy>>(
            RawData::BufferManagerConfig::create().get() );
        mCommandSchema =
            std::make_unique<CommandSchema>( mReceiverCommandRequest, mCommandResponses, mRawBufferManagerSpy );
        mCommandSchema->subscribeToActuatorCommandRequestReceived( [&]( const ActuatorCommandRequest &commandRequest ) {
            mReceivedActuatorCommandRequests.emplace_back( commandRequest );
        } );
        mCommandSchema->subscribeToLastKnownStateCommandRequestReceived(
            [&]( const LastKnownStateCommandRequest &commandRequest ) {
                mReceivedLastKnownStateCommandRequests.emplace_back( commandRequest );
            } );
    }

    static Aws::Crt::Mqtt5::PublishReceivedEventData
    createPublishEvent( const std::string &protoSerializedBuffer )
    {
        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>();
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;
        eventData.publishPacket = publishPacket;
        publishPacket->WithPayload( Aws::Crt::ByteCursorFromArray(
            reinterpret_cast<const uint8_t *>( protoSerializedBuffer.data() ), protoSerializedBuffer.length() ) );

        return eventData;
    }

    static void
    setSignalValue( Schemas::Commands::ActuatorCommand *protoActuatorCommand,
                    double signalValue,
                    SignalType signalType )
    {
        // Note: protobuf smallest integer is 32 bits, that is why we cast the smaller types to int32 and uint32 below
        switch ( signalType )
        {
        case SignalType::UINT8:
            protoActuatorCommand->set_uint8_value( static_cast<uint32_t>( signalValue ) );
            break;
        case SignalType::INT8:
            protoActuatorCommand->set_int8_value( static_cast<int32_t>( signalValue ) );
            break;
        case SignalType::UINT16:
            protoActuatorCommand->set_uint16_value( static_cast<uint32_t>( signalValue ) );
            break;
        case SignalType::INT16:
            protoActuatorCommand->set_int16_value( static_cast<int32_t>( signalValue ) );
            break;
        case SignalType::UINT32:
            protoActuatorCommand->set_uint32_value( static_cast<uint32_t>( signalValue ) );
            break;
        case SignalType::INT32:
            protoActuatorCommand->set_int32_value( static_cast<int32_t>( signalValue ) );
            break;
        case SignalType::UINT64:
            protoActuatorCommand->set_uint64_value( static_cast<uint64_t>( signalValue ) );
            break;
        case SignalType::INT64:
            protoActuatorCommand->set_int64_value( static_cast<int64_t>( signalValue ) );
            break;
        case SignalType::FLOAT:
            protoActuatorCommand->set_float_value( static_cast<float>( signalValue ) );
            break;
        case SignalType::DOUBLE:
            protoActuatorCommand->set_double_value( signalValue );
            break;
        case SignalType::BOOLEAN:
            protoActuatorCommand->set_boolean_value( static_cast<bool>( signalValue ) );
            break;
        default:
            FAIL() << "Unsupported signal type";
        }
    }

    bool
    popCommandResponse( std::shared_ptr<const CommandResponse> &commandResponse )
    {
        std::shared_ptr<const DataToSend> senderData;
        auto succeeded = mCommandResponses->pop( senderData );
        commandResponse = std::dynamic_pointer_cast<const CommandResponse>( senderData );
        return succeeded;
    }

    std::unique_ptr<TopicConfig> mTopicConfig;
    std::unique_ptr<AwsIotConnectivityModule> mAwsIotModule;
    std::shared_ptr<AwsIotReceiver> mReceiverCommandRequest;
    std::shared_ptr<DataSenderQueue> mCommandResponses;
    std::shared_ptr<NiceMock<Testing::RawDataBufferManagerSpy>> mRawBufferManagerSpy;
    std::unique_ptr<CommandSchema> mCommandSchema;

    std::vector<ActuatorCommandRequest> mReceivedActuatorCommandRequests;
    std::vector<LastKnownStateCommandRequest> mReceivedLastKnownStateCommandRequests;
};

TEST_F( CommandSchemaTest, ingestEmptyCommandRequest )
{
    std::string protoSerializedBuffer;

    mReceiverCommandRequest->onDataReceived( createPublishEvent( protoSerializedBuffer ) );

    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );
    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 0 );
}

TEST_F( CommandSchemaTest, ingestCommandRequestLargerThanLimit )
{
    std::string protoSerializedBuffer( COMMAND_REQUEST_BYTE_SIZE_LIMIT + 1, 'X' );

    mReceiverCommandRequest->onDataReceived( createPublishEvent( protoSerializedBuffer ) );

    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );
    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 0 );
}

TEST_F( CommandSchemaTest, ingestActuatorCommandRequest )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    protoCommandRequest.set_timeout_ms( 5000 );
    auto issuedTimestampMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    protoCommandRequest.set_issued_timestamp_ms( issuedTimestampMs );

    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    protoActuatorCommand->set_signal_id( 12345 );
    protoActuatorCommand->set_double_value( 12.5 );

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 1 );
    auto commandRequest = mReceivedActuatorCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.signalID, 12345 );
    ASSERT_EQ( commandRequest.signalValueWrapper.type, SignalType::DOUBLE );
    ASSERT_EQ( commandRequest.signalValueWrapper.value.doubleVal, 12.5 );
    ASSERT_EQ( commandRequest.executionTimeoutMs, 5000 );
    ASSERT_EQ( commandRequest.issuedTimestampMs, issuedTimestampMs );
}

TEST_F( CommandSchemaTest, ingestActuatorCommandRequestWithInvalidValue )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    // Don't set any value, only the signal ID
    protoActuatorCommand->set_signal_id( 12345 );
    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command123" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_COMMAND_REQUEST_PARSING_FAILED );
    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );

    // Now set a string value with an unsupported signal id
    protoActuatorCommand->set_signal_id( 12345 );
    protoActuatorCommand->set_string_value( "some string value" );
    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command123" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_REJECTED );
    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );

    // Now just as sanity check set a valid value to ensure it was not failing for some other reason
    protoActuatorCommand->set_signal_id( 12345 );
    protoActuatorCommand->set_double_value( 12.5 );
    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 1U );
    auto commandRequest = mReceivedActuatorCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.signalID, 12345 );
    ASSERT_EQ( commandRequest.signalValueWrapper.type, SignalType::DOUBLE );
    ASSERT_EQ( commandRequest.signalValueWrapper.value.doubleVal, 12.5 );
}

struct TestSignal
{
    TestSignal( double value, SignalType type )
        : value( value )
        , type( type )
    {
    }

    double value{ 0.0 };
    SignalType type{ SignalType::DOUBLE };
    // This is a dummy variable just to prevent the compiler from adding padding. Since this struct is being
    // used as a test parameter, gtest tries to print its bytes. If there is any padding added by the compiler,
    // gtest will try to read it even though it is normally uninitialized, which will be detected by valgrind.
    // More details: https://github.com/google/googletest/issues/3805
    uint32_t padding{ 0 };

    static std::string
    toString( const ::testing::TestParamInfo<TestSignal> &info )
    {
        // Test names can only contain alphanumeric characters, so we can't just convert a double to string
        auto doubleValueAsString = std::regex_replace( std::to_string( info.param.value ), std::regex( "\\." ), "d" );
        doubleValueAsString = std::regex_replace( doubleValueAsString, std::regex( "-" ), "n" );
        return doubleValueAsString + signalTypeParamInfoToString( { info.param.type, info.index } );
    }
};

class CommandSchemaTestWithAllSignalTypes : public CommandSchemaTest, public testing::WithParamInterface<TestSignal>
{
};

INSTANTIATE_TEST_SUITE_P( MultipleSignals,
                          CommandSchemaTestWithAllSignalTypes,
                          testing::Values( TestSignal{ static_cast<double>( UINT8_MAX ), SignalType::UINT8 },
                                           TestSignal{ static_cast<double>( INT8_MIN ), SignalType::INT8 },
                                           TestSignal{ static_cast<double>( INT8_MAX ), SignalType::INT8 },
                                           TestSignal{ static_cast<double>( UINT16_MAX ), SignalType::UINT16 },
                                           TestSignal{ static_cast<double>( INT16_MIN ), SignalType::INT16 },
                                           TestSignal{ static_cast<double>( INT16_MAX ), SignalType::INT16 },
                                           TestSignal{ static_cast<double>( UINT32_MAX ), SignalType::UINT32 },
                                           TestSignal{ static_cast<double>( INT32_MIN ), SignalType::INT32 },
                                           TestSignal{ static_cast<double>( INT32_MAX ), SignalType::INT32 },
                                           TestSignal{ static_cast<double>( UINT64_MAX ), SignalType::UINT64 },
                                           TestSignal{ static_cast<double>( INT64_MIN ), SignalType::INT64 },
                                           TestSignal{ static_cast<double>( INT64_MAX ), SignalType::INT64 },
                                           TestSignal{ static_cast<double>( FLT_MIN ), SignalType::FLOAT },
                                           TestSignal{ static_cast<double>( -FLT_MAX ), SignalType::FLOAT },
                                           TestSignal{ static_cast<double>( FLT_MAX ), SignalType::FLOAT },
                                           TestSignal{ static_cast<double>( DBL_MIN ), SignalType::DOUBLE },
                                           TestSignal{ static_cast<double>( -DBL_MAX ), SignalType::DOUBLE },
                                           TestSignal{ static_cast<double>( DBL_MAX ), SignalType::DOUBLE },
                                           TestSignal{ static_cast<double>( 0.0 ), SignalType::BOOLEAN },
                                           TestSignal{ static_cast<double>( 1.0 ), SignalType::BOOLEAN } ),
                          TestSignal::toString );

TEST_P( CommandSchemaTestWithAllSignalTypes, ingestActuatorCommandRequestWithAllSignalTypes )
{
    TestSignal signal = GetParam();
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    protoActuatorCommand->set_signal_id( 12345 );
    setSignalValue( protoActuatorCommand, signal.value, signal.type );

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 1U );
    auto commandRequest = mReceivedActuatorCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.signalID, 12345 );
    ASSERT_EQ( commandRequest.signalValueWrapper.type, signal.type );
    ASSERT_NO_FATAL_FAILURE( assertSignalValue( commandRequest.signalValueWrapper, signal.value, signal.type ) );
}

class CommandSchemaTestWithOutOfRangeSignals : public CommandSchemaTest, public testing::WithParamInterface<TestSignal>
{
};

INSTANTIATE_TEST_SUITE_P( MultipleSignals,
                          CommandSchemaTestWithOutOfRangeSignals,
                          testing::Values( TestSignal{ static_cast<double>( UINT8_MAX + 1 ), SignalType::UINT8 },
                                           TestSignal{ static_cast<double>( INT8_MIN - 1 ), SignalType::INT8 },
                                           TestSignal{ static_cast<double>( INT8_MAX + 1 ), SignalType::INT8 },
                                           TestSignal{ static_cast<double>( UINT16_MAX + 1 ), SignalType::UINT16 },
                                           TestSignal{ static_cast<double>( INT16_MIN - 1 ), SignalType::INT16 },
                                           TestSignal{ static_cast<double>( INT16_MAX + 1 ), SignalType::INT16 } ),
                          TestSignal::toString );

TEST_P( CommandSchemaTestWithOutOfRangeSignals, ingestActuatorCommandRequestWithOutOfRangeSignal )
{
    TestSignal signal = GetParam();
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    protoActuatorCommand->set_signal_id( 12345 );
    setSignalValue( protoActuatorCommand, signal.value, signal.type );

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );
}

TEST_F( CommandSchemaTest, ingestCommandAlreadyTimedOut )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    protoCommandRequest.set_issued_timestamp_ms( ClockHandler::getClock()->systemTimeSinceEpochMs() - 1000 );
    protoCommandRequest.set_timeout_ms( 500 );
    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    protoActuatorCommand->set_signal_id( 12345 );
    protoActuatorCommand->set_uint32_value( 1234 );
    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );
    auto publishEvent = createPublishEvent( protoSerializedBuffer );
    mReceiverCommandRequest->onDataReceived( publishEvent );
    std::shared_ptr<const CommandResponse> commandResponse;
    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command123" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_TIMEOUT );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_TIMED_OUT_BEFORE_DISPATCH );
    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );
}

TEST_F( CommandSchemaTest, ingestActuatorCommandRequestWithStringSignalWithBadSignalId )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    protoActuatorCommand->set_signal_id( 12345 );
    protoActuatorCommand->set_string_value( "some string value" );
    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );
    auto publishEvent = createPublishEvent( protoSerializedBuffer );
    mReceiverCommandRequest->onDataReceived( publishEvent );
    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 0 );
}

TEST_F( CommandSchemaTest, ingestActuatorCommandRequestWithStringSignal )
{
    mRawBufferManagerSpy->updateConfig( { { 12345, { 12345, "", "" } } } );
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );
    // Check setting issued time in future just produces warning
    protoCommandRequest.set_issued_timestamp_ms( ClockHandler::getClock()->systemTimeSinceEpochMs() + 1000 );
    auto *protoActuatorCommand = protoCommandRequest.mutable_actuator_command();
    protoActuatorCommand->set_signal_id( 12345 );
    protoActuatorCommand->set_string_value( "some string value" );
    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );
    auto publishEvent = createPublishEvent( protoSerializedBuffer );
    mReceiverCommandRequest->onDataReceived( publishEvent );
    ASSERT_EQ( mReceivedActuatorCommandRequests.size(), 1U );
    auto commandRequest = mReceivedActuatorCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.signalID, 12345 );
    ASSERT_EQ( commandRequest.signalValueWrapper.type, SignalType::STRING );
    auto loanedFrame = mRawBufferManagerSpy->borrowFrame( commandRequest.signalValueWrapper.value.rawDataVal.signalId,
                                                          commandRequest.signalValueWrapper.value.rawDataVal.handle );
    ASSERT_FALSE( loanedFrame.isNull() );
    std::string receivedStringVal;
    receivedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( receivedStringVal, "some string value" );
}

TEST_F( CommandSchemaTest, ingestLastKnownStateActivateCommandRequest )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );

    auto *protoLastKnownStateCommand = protoCommandRequest.mutable_last_known_state_command();
    auto *protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->mutable_activate_operation();

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 1 );
    auto commandRequest = mReceivedLastKnownStateCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks1" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::ACTIVATE );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );
}

TEST_F( CommandSchemaTest, ingestLastKnownStateActivateCommandRequestWithAutoDeactivate )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );

    auto *protoLastKnownStateCommand = protoCommandRequest.mutable_last_known_state_command();
    auto *protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    auto *protoActivateOperation = protoStateTemplateInfo->mutable_activate_operation();
    protoActivateOperation->set_deactivate_after_seconds( 432 );

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 1 );
    auto commandRequest = mReceivedLastKnownStateCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks1" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::ACTIVATE );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 432 );
}

TEST_F( CommandSchemaTest, ingestLastKnownStateDeactivateCommandRequest )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );

    auto *protoLastKnownStateCommand = protoCommandRequest.mutable_last_known_state_command();
    auto *protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->mutable_deactivate_operation();

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 1 );
    auto commandRequest = mReceivedLastKnownStateCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks1" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::DEACTIVATE );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );
}

TEST_F( CommandSchemaTest, ingestLastKnownStateFetchCommandRequest )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );

    auto *protoLastKnownStateCommand = protoCommandRequest.mutable_last_known_state_command();
    auto *protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->mutable_fetch_snapshot_operation();

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 1 );
    auto commandRequest = mReceivedLastKnownStateCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks1" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::FETCH_SNAPSHOT );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );
}

TEST_F( CommandSchemaTest, ingestMultipleLastKnownStateCommandRequest )
{
    Schemas::Commands::CommandRequest protoCommandRequest;
    protoCommandRequest.set_command_id( "command123" );

    auto *protoLastKnownStateCommand = protoCommandRequest.mutable_last_known_state_command();
    auto *protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->mutable_activate_operation();

    protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks2" );
    protoStateTemplateInfo->mutable_deactivate_operation();

    protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks3" );
    protoStateTemplateInfo->mutable_fetch_snapshot_operation();

    protoStateTemplateInfo = protoLastKnownStateCommand->add_state_template_information();
    protoStateTemplateInfo->set_state_template_sync_id( "lks4" );
    protoStateTemplateInfo->mutable_deactivate_operation();

    std::string protoSerializedBuffer;

    ASSERT_TRUE( protoCommandRequest.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );

    mReceiverCommandRequest->onDataReceived( publishEvent );

    ASSERT_EQ( mReceivedLastKnownStateCommandRequests.size(), 4 );
    auto commandRequest = mReceivedLastKnownStateCommandRequests[0];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks1" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::ACTIVATE );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );

    commandRequest = mReceivedLastKnownStateCommandRequests[1];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks2" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::DEACTIVATE );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );

    commandRequest = mReceivedLastKnownStateCommandRequests[2];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks3" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::FETCH_SNAPSHOT );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );

    commandRequest = mReceivedLastKnownStateCommandRequests[3];
    ASSERT_EQ( commandRequest.commandID, "command123" );
    ASSERT_EQ( commandRequest.stateTemplateID, "lks4" );
    ASSERT_EQ( commandRequest.operation, LastKnownStateOperation::DEACTIVATE );
    ASSERT_EQ( commandRequest.deactivateAfterSeconds, 0 );
}

} // namespace IoTFleetWise
} // namespace Aws
