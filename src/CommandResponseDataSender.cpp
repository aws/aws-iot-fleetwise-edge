// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CommandResponseDataSender.h"
#include "aws/iotfleetwise/CommandTypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <utility>

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

CommandResponseDataSender::CommandResponseDataSender( ISender &sender )
    : mMqttSender( sender )
{
}

bool
CommandResponseDataSender::isAlive()
{
    return mMqttSender.isAlive();
}

void
CommandResponseDataSender::processData( const DataToSend &data, OnDataProcessedCallback callback )
{
    // coverity[autosar_cpp14_a5_2_1_violation] Cast by design as we want the sender to know the concrete type.
    // coverity[autosar_cpp14_m5_2_3_violation] Cast by design as we want the sender to know the concrete type.
    // coverity[misra_cpp_2008_rule_5_2_3_violation] Cast by design as we want the sender to know the concrete type.
    auto commandResponse = dynamic_cast<const CommandResponse *>( &data );
    if ( commandResponse == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is not a valid CommandResponse" );
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

    mMqttSender.sendBuffer(
        mMqttSender.getTopicConfig().commandResponseTopic( commandResponse->id ),
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
CommandResponseDataSender::processPersistedData( const uint8_t *buf,
                                                 size_t size,
                                                 const Json::Value &metadata,
                                                 OnPersistedDataProcessedCallback callback )
{
    auto commandID = metadata["commandID"].asString();

    if ( !mMqttSender.isAlive() )
    {
        callback( false );
        return;
    }

    mMqttSender.sendBuffer(
        mMqttSender.getTopicConfig().commandResponseTopic( commandID ),
        buf,
        size,
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
