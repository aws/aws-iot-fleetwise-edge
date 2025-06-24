// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CommandSchema.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include "command_request.pb.h"
#include <cstdint>
#include <google/protobuf/message.h>
#include <json/json.h>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

static CommandReasonCode
setSignalValue( const Schemas::Commands::ActuatorCommand &protoActuatorCommand,
                ActuatorCommandRequest &commandRequest,
                Timestamp receivedTime,
                RawData::BufferManager *rawDataBufferManager )
{
    if ( protoActuatorCommand.has_double_value() )
    {
        commandRequest.signalValueWrapper.setVal<double>( protoActuatorCommand.double_value(), SignalType::DOUBLE );
    }
    else if ( protoActuatorCommand.has_float_value() )
    {
        commandRequest.signalValueWrapper.setVal<float>( protoActuatorCommand.float_value(), SignalType::FLOAT );
    }
    else if ( protoActuatorCommand.has_boolean_value() )
    {
        commandRequest.signalValueWrapper.setVal<bool>( protoActuatorCommand.boolean_value(), SignalType::BOOLEAN );
    }
    else if ( protoActuatorCommand.has_uint8_value() )
    {
        auto value = protoActuatorCommand.uint8_value();
        if ( value > UINT8_MAX )
        {
            FWE_LOG_ERROR( "Invalid command '" + commandRequest.commandID +
                           "', uint8 value out of range: " + std::to_string( value ) );
            return REASON_CODE_ARGUMENT_OUT_OF_RANGE;
        }
        commandRequest.signalValueWrapper.setVal<uint8_t>( static_cast<uint8_t>( value ), SignalType::UINT8 );
    }
    else if ( protoActuatorCommand.has_int8_value() )
    {
        auto value = protoActuatorCommand.int8_value();
        if ( ( value < INT8_MIN ) || ( value > INT8_MAX ) )
        {
            FWE_LOG_ERROR( "Invalid command '" + commandRequest.commandID +
                           "', int8 value out of range: " + std::to_string( value ) );
            return REASON_CODE_ARGUMENT_OUT_OF_RANGE;
        }
        commandRequest.signalValueWrapper.setVal<int8_t>( static_cast<int8_t>( value ), SignalType::INT8 );
    }
    else if ( protoActuatorCommand.has_uint16_value() )
    {
        auto value = protoActuatorCommand.uint16_value();
        if ( value > UINT16_MAX )
        {
            FWE_LOG_ERROR( "Invalid command '" + commandRequest.commandID +
                           "', uint16 value out of range: " + std::to_string( value ) );
            return REASON_CODE_ARGUMENT_OUT_OF_RANGE;
        }
        commandRequest.signalValueWrapper.setVal<uint16_t>( static_cast<uint16_t>( value ), SignalType::UINT16 );
    }
    else if ( protoActuatorCommand.has_int16_value() )
    {
        auto value = protoActuatorCommand.int16_value();
        if ( ( value < INT16_MIN ) || ( value > INT16_MAX ) )
        {
            FWE_LOG_ERROR( "Invalid command '" + commandRequest.commandID +
                           "', int16 value out of range: " + std::to_string( value ) );
            return REASON_CODE_ARGUMENT_OUT_OF_RANGE;
        }
        commandRequest.signalValueWrapper.setVal<int16_t>( static_cast<int16_t>( value ), SignalType::INT16 );
    }
    else if ( protoActuatorCommand.has_uint32_value() )
    {
        commandRequest.signalValueWrapper.setVal<uint32_t>( protoActuatorCommand.uint32_value(), SignalType::UINT32 );
    }
    else if ( protoActuatorCommand.has_int32_value() )
    {
        commandRequest.signalValueWrapper.setVal<int32_t>( protoActuatorCommand.int32_value(), SignalType::INT32 );
    }
    else if ( protoActuatorCommand.has_uint64_value() )
    {
        commandRequest.signalValueWrapper.setVal<uint64_t>( protoActuatorCommand.uint64_value(), SignalType::UINT64 );
    }
    else if ( protoActuatorCommand.has_int64_value() )
    {
        commandRequest.signalValueWrapper.setVal<int64_t>( protoActuatorCommand.int64_value(), SignalType::INT64 );
    }
    else if ( protoActuatorCommand.has_string_value() )
    {
        auto signalId = protoActuatorCommand.signal_id();
        if ( rawDataBufferManager == nullptr )
        {
            FWE_LOG_WARN( "Signal id " + std::to_string( signalId ) +
                          " is of type string type but there is no RawBufferManager" );
            return REASON_CODE_REJECTED;
        }
        auto bufferHandle =
            rawDataBufferManager->push( reinterpret_cast<const uint8_t *>( protoActuatorCommand.string_value().data() ),
                                        protoActuatorCommand.string_value().size(),
                                        receivedTime,
                                        signalId );
        if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
        {
            FWE_LOG_WARN( "Signal id " + std::to_string( signalId ) + "  was rejected by RawBufferManager" );
            return REASON_CODE_REJECTED;
        }
        // immediately set usage hint so buffer handle does not get directly deleted again
        rawDataBufferManager->increaseHandleUsageHint(
            signalId, bufferHandle, RawData::BufferHandleUsageStage::UPLOADING );
        commandRequest.signalValueWrapper.setVal<SignalValue::RawDataVal>(
            SignalValue::RawDataVal{ signalId, bufferHandle }, SignalType::STRING );
    }
    else
    {
        FWE_LOG_ERROR( "Invalid command '" + commandRequest.commandID + "', none of the expected value fields is set" );
        return REASON_CODE_COMMAND_REQUEST_PARSING_FAILED;
    }

    return REASON_CODE_UNSPECIFIED;
}

CommandSchema::CommandSchema( IReceiver &receiverCommandRequest,
                              std::shared_ptr<DataSenderQueue> commandResponses,
                              RawData::BufferManager *rawDataBufferManager )
    : mCommandResponses( std::move( commandResponses ) )
    , mRawDataBufferManager( rawDataBufferManager )
{
    // Register the listeners
    receiverCommandRequest.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onCommandRequestReceived( receivedMessage );
    } );
}

void
CommandSchema::onCommandRequestReceived( const ReceivedConnectivityMessage &receivedMessage )
{
    // Check for a empty input data
    if ( ( receivedMessage.buf == nullptr ) || ( receivedMessage.size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty command data from Cloud" );
        return;
    }

    TraceModule::get().incrementVariable( TraceVariable::COMMAND_REQUESTS_RECEIVED );

    Schemas::Commands::CommandRequest protoCommandRequest;
    // Verify we have not accidentally linked against a version of the library which is incompatible with the version of
    // the headers we compiled with.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if ( !protoCommandRequest.ParseFromArray( receivedMessage.buf, static_cast<int>( receivedMessage.size ) ) )
    {
        FWE_LOG_ERROR( "Failed to parse CommandRequest proto" );
        TraceModule::get().incrementVariable( TraceVariable::COMMAND_REQUEST_PARSING_FAILURE );
        mCommandResponses->push( std::make_shared<CommandResponse>(
            "", CommandStatus::EXECUTION_FAILED, REASON_CODE_COMMAND_REQUEST_PARSING_FAILED, "" ) );
        return;
    }

    // Check if command has already timed out when it was received:
    auto currentTimeMs = mClock->systemTimeSinceEpochMs();
    auto issuedTimestampMs = protoCommandRequest.issued_timestamp_ms();
    if ( issuedTimestampMs == 0 ) // TODO: Remove this if once cloud supports issued_timestamp_ms
    {
        issuedTimestampMs = currentTimeMs;
    }
    if ( currentTimeMs < issuedTimestampMs )
    {
        FWE_LOG_WARN( "Issued time " + std::to_string( issuedTimestampMs ) + " is later than current time " +
                      std::to_string( currentTimeMs ) + " for command id " + protoCommandRequest.command_id() );
    }
    if ( ( protoCommandRequest.timeout_ms() > 0 ) &&
         ( ( issuedTimestampMs + protoCommandRequest.timeout_ms() ) <= currentTimeMs ) )
    {
        FWE_LOG_ERROR( "Command Request with ID " + protoCommandRequest.command_id() + " timed out" );
        TraceModule::get().incrementVariable( TraceVariable::COMMAND_EXECUTION_TIMEOUT_BEFORE_DISPATCH );
        mCommandResponses->push( std::make_shared<CommandResponse>( protoCommandRequest.command_id(),
                                                                    CommandStatus::EXECUTION_TIMEOUT,
                                                                    REASON_CODE_TIMED_OUT_BEFORE_DISPATCH,
                                                                    "" ) );
        return;
    }

    if ( protoCommandRequest.has_actuator_command() )
    {
        FWE_LOG_INFO( "Building ActuatorCommandRequest with ID: " + protoCommandRequest.command_id() );
        ActuatorCommandRequest commandRequest;

        auto &protoActuatorCommand = protoCommandRequest.actuator_command();

        commandRequest.commandID = protoCommandRequest.command_id();
        commandRequest.signalID = protoActuatorCommand.signal_id();
        commandRequest.issuedTimestampMs = issuedTimestampMs;
        commandRequest.executionTimeoutMs = protoCommandRequest.timeout_ms();
        commandRequest.decoderID = protoActuatorCommand.decoder_manifest_sync_id();

        auto reasonCode = setSignalValue( protoActuatorCommand, commandRequest, currentTimeMs, mRawDataBufferManager );
        if ( reasonCode != REASON_CODE_UNSPECIFIED )
        {
            TraceModule::get().incrementVariable( TraceVariable::COMMAND_SETTING_SIGNAL_VALUE_FAILURE );
            mCommandResponses->push( std::make_shared<CommandResponse>(
                commandRequest.commandID, CommandStatus::EXECUTION_FAILED, reasonCode, "" ) );
            return;
        }

        mActuatorCommandRequestListeners.notify( std::move( commandRequest ) );
    }
    else if ( protoCommandRequest.has_last_known_state_command() )
    {
        FWE_LOG_INFO( "Building LastKnownStateCommandRequest with ID: " + protoCommandRequest.command_id() );
        auto &protoLastKnownStateCommand = protoCommandRequest.last_known_state_command();

        if ( protoLastKnownStateCommand.state_template_information_size() == 0 )
        {
            FWE_LOG_ERROR( "Invalid command '" + protoCommandRequest.command_id() +
                           "', no state template information provided" );
            return;
        }

        for ( const auto &stateTemplateInformation : protoLastKnownStateCommand.state_template_information() )
        {
            LastKnownStateCommandRequest commandRequest;
            commandRequest.commandID = protoCommandRequest.command_id();
            commandRequest.stateTemplateID = stateTemplateInformation.state_template_sync_id();
            commandRequest.receivedTime = mClock->timeSinceEpoch();

            if ( stateTemplateInformation.has_activate_operation() )
            {
                commandRequest.operation = LastKnownStateOperation::ACTIVATE;
                commandRequest.deactivateAfterSeconds =
                    stateTemplateInformation.activate_operation().deactivate_after_seconds();
            }
            else if ( stateTemplateInformation.has_deactivate_operation() )
            {
                commandRequest.operation = LastKnownStateOperation::DEACTIVATE;
            }
            else if ( stateTemplateInformation.has_fetch_snapshot_operation() )
            {
                commandRequest.operation = LastKnownStateOperation::FETCH_SNAPSHOT;
            }
            else
            {
                FWE_LOG_ERROR( "Invalid state template information for state template ID '" +
                               commandRequest.stateTemplateID + "' and command ID '" + commandRequest.commandID +
                               "', none of the expected operation fields is present" );
                continue;
            }

            mLastKnownStateCommandRequestListeners.notify( std::move( commandRequest ) );
        }
    }
    else
    {
        FWE_LOG_ERROR( "Invalid command '" + protoCommandRequest.command_id() +
                       "', none of the expected command types is present" );
    }
}

void
CommandSchema::onRejectedCommandResponseReceived( const ReceivedConnectivityMessage &receivedMessage )
{
    Json::Reader reader;
    Json::Value root;
    if ( !reader.parse( std::string( receivedMessage.buf, receivedMessage.buf + receivedMessage.size ), root ) )
    {
        FWE_LOG_ERROR( "A command response was rejected, but the rejected message could not be parsed." );
        return;
    }

    std::string error;
    if ( root.isMember( "error" ) )
    {
        error = root["error"].asString();
    }

    std::string errorMessage;
    if ( root.isMember( "errorMessage" ) )
    {
        errorMessage = root["errorMessage"].asString();
    }

    FWE_LOG_ERROR( "A command response was rejected. Error: '" + error + "', Message: '" + errorMessage + "'" );
}

} // namespace IoTFleetWise
} // namespace Aws
