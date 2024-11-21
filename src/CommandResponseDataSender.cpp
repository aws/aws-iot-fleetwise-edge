// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandResponseDataSender.h"
#include "CommandTypes.h"
#include "ICommandDispatcher.h"
#include "IConnectionTypes.h"
#include "LoggingModule.h"
#include "TopicConfig.h"
#include "TraceModule.h"
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class CommandResponseDataToPersist : public DataToPersist
{
public:
    CommandResponseDataToPersist( CommandID commandID, std::shared_ptr<std::string> data )
        : mCommandID( std::move( commandID ) )
        , mData( std::move( data ) )
    {
    }

    SenderDataType
    getDataType() const override
    {
        return SenderDataType::COMMAND_RESPONSE;
    }

    Json::Value
    getMetadata() const override
    {
        Json::Value metadata;
        metadata["commandID"] = mCommandID;
        return metadata;
    }

    std::string
    getFilename() const override
    {
        return "command-" + mCommandID + ".bin";
    };

    boost::variant<std::shared_ptr<std::string>, std::shared_ptr<std::streambuf>>
    getData() const override
    {
        return mData;
    }

private:
    CommandID mCommandID;
    std::shared_ptr<std::string> mData;
};

static Schemas::Commands::Status
internalCommandStatusToProto( CommandStatus status )
{
    // coverity[misra_cpp_2008_rule_6_4_6_violation] compiler warning is preferred over a default-clause
    switch ( status )
    {
    case CommandStatus::SUCCEEDED:
        return Schemas::Commands::COMMAND_STATUS_SUCCEEDED;
    case CommandStatus::EXECUTION_TIMEOUT:
        return Schemas::Commands::COMMAND_STATUS_EXECUTION_TIMEOUT;
    case CommandStatus::EXECUTION_FAILED:
        return Schemas::Commands::COMMAND_STATUS_EXECUTION_FAILED;
    case CommandStatus::IN_PROGRESS:
        return Schemas::Commands::COMMAND_STATUS_IN_PROGRESS;
    }
    return Schemas::Commands::COMMAND_STATUS_UNSPECIFIED;
}

CommandResponseDataSender::CommandResponseDataSender( std::shared_ptr<ISender> commandResponseSender )
    : mCommandResponseSender( std::move( commandResponseSender ) )
{
}

void
CommandResponseDataSender::processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback )
{
    if ( data == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is empty" );
        return;
    }

    auto commandResponse = std::dynamic_pointer_cast<const CommandResponse>( data );
    if ( commandResponse == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is not a valid CommandResponse" );
        return;
    }

    if ( mCommandResponseSender == nullptr )
    {
        FWE_LOG_ERROR( "No sender for command response provided" );
        return;
    }

    FWE_LOG_INFO( "Ready to send response for command with ID: " + commandResponse->id );

    auto protoStatus = internalCommandStatusToProto( commandResponse->status );
    if ( protoStatus == Schemas::Commands::COMMAND_STATUS_UNSPECIFIED )
    {
        FWE_LOG_ERROR( "Unknown command status: " + std::to_string( static_cast<int>( commandResponse->status ) ) );
        return;
    }

    mProtoCommandResponseMsg.set_command_id( commandResponse->id );
    mProtoCommandResponseMsg.set_status( protoStatus );
    mProtoCommandResponseMsg.set_reason_code( commandResponse->reasonCode );
    mProtoCommandResponseMsg.set_reason_description( commandResponse->reasonDescription );

    auto protoOutput = std::make_shared<std::string>();

    if ( !mProtoCommandResponseMsg.SerializeToString( &( *protoOutput ) ) )
    {
        FWE_LOG_ERROR( "Serialization failed for command response with ID: " + commandResponse->id );
        return;
    }

    mCommandResponseSender->sendBuffer(
        mCommandResponseSender->getTopicConfig().commandResponseTopic( commandResponse->id ),
        reinterpret_cast<const uint8_t *>( protoOutput->data() ),
        protoOutput->size(),
        [protoOutput, commandId = commandResponse->id, callback]( ConnectivityError result ) {
            if ( result == ConnectivityError::Success )
            {
                FWE_LOG_INFO( "A command response payload of size: " + std::to_string( protoOutput->size() ) +
                              " bytes has been uploaded for command ID: " + commandId );
                callback( true, nullptr );
            }
            else
            {
                FWE_LOG_ERROR( "Failed to send command response for command ID: " + commandId +
                               " with error: " + std::to_string( static_cast<int>( result ) ) );
                callback( false, std::make_shared<CommandResponseDataToPersist>( commandId, protoOutput ) );
            }
        },
        QoS::AT_LEAST_ONCE );

    TraceModule::get().incrementVariable( TraceVariable::MQTT_COMMAND_RESPONSE_MESSAGE_SENT_OUT );
    TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_PENDING_COMMAND_RESPONSES );
}

void
CommandResponseDataSender::processPersistedData( std::istream &data,
                                                 const Json::Value &metadata,
                                                 OnPersistedDataProcessedCallback callback )
{
    auto commandID = metadata["commandID"].asString();

    if ( !mCommandResponseSender->isAlive() )
    {
        callback( false );
        return;
    }

    data.seekg( 0, std::ios::end );
    auto size = data.tellg();
    auto dataAsArray = std::vector<char>( static_cast<size_t>( size ) );
    data.seekg( 0, std::ios::beg );
    data.read( dataAsArray.data(), static_cast<std::streamsize>( size ) );

    if ( !data.good() )
    {
        FWE_LOG_ERROR( "Failed to read persisted command response for commandID '" + commandID + "'" );
        callback( false );
        return;
    }

    auto buf = reinterpret_cast<const uint8_t *>( dataAsArray.data() );
    auto bufSize = static_cast<size_t>( size );
    mCommandResponseSender->sendBuffer(
        mCommandResponseSender->getTopicConfig().commandResponseTopic( commandID ),
        buf,
        bufSize,
        [callback, size]( ConnectivityError result ) {
            if ( result != ConnectivityError::Success )
            {
                callback( false );
                return;
            }

            FWE_LOG_INFO( "A Payload of size: " + std::to_string( size ) + " bytes has been uploaded" );
            callback( true );
        },
        QoS::AT_LEAST_ONCE );
}

} // namespace IoTFleetWise
} // namespace Aws
