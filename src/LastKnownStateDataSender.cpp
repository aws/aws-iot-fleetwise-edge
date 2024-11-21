// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LastKnownStateDataSender.h"
#include "CollectionInspectionAPITypes.h"
#include "IConnectionTypes.h"
#include "LastKnownStateTypes.h"
#include "LoggingModule.h"
#include "SignalTypes.h"
#include "TopicConfig.h"
#include "TraceModule.h"
#include <cstdint>
#include <google/protobuf/message.h>
#include <snappy.h>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

LastKnownStateDataSender::LastKnownStateDataSender( std::shared_ptr<ISender> lastKnownStateSender,
                                                    unsigned maxMessagesPerPayload )
    : mLastKnownStateSender( std::move( lastKnownStateSender ) )
    , mMaxMessagesPerPayload( maxMessagesPerPayload )
{
}

void
LastKnownStateDataSender::processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback )
{
    if ( data == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is empty" );
        return;
    }

    auto collectedData = std::dynamic_pointer_cast<const LastKnownStateCollectedData>( data );
    if ( collectedData == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is not a valid LastKnownStateCollectedData" );
        return;
    }

    if ( mLastKnownStateSender == nullptr )
    {
        FWE_LOG_ERROR( "No sender for LastKnownState data provided" );
        return;
    }

    resetProto( collectedData->triggerTime );

    std::stringstream logMessageIds;

    for ( auto &stateTemplateCollectedSignals : collectedData->stateTemplateCollectedSignals )
    {
        logMessageIds << stateTemplateCollectedSignals.stateTemplateSyncId << " ";

        auto capturedStateTemplateSignals = mProtoMsg.add_captured_state_template_signals();
        capturedStateTemplateSignals->set_state_template_sync_id( stateTemplateCollectedSignals.stateTemplateSyncId );
        auto capturedSignalsProto = capturedStateTemplateSignals->mutable_captured_signals();

        FWE_LOG_INFO( "Ready to send state template data with ID: " +
                      stateTemplateCollectedSignals.stateTemplateSyncId );
        for ( auto &signal : stateTemplateCollectedSignals.signals )
        {
            Schemas::LastKnownState::CapturedSignal signalProto;
            signalProto.set_signal_id( signal.signalID );

            auto signalValueWrapper = signal.value;

            switch ( signalValueWrapper.getType() )
            {
            case SignalType::UINT8:
                signalProto.set_uint8_value( signalValueWrapper.value.uint8Val );
                break;
            case SignalType::INT8:
                signalProto.set_int8_value( signalValueWrapper.value.int8Val );
                break;
            case SignalType::UINT16:
                signalProto.set_uint16_value( signalValueWrapper.value.uint16Val );
                break;
            case SignalType::INT16:
                signalProto.set_int16_value( signalValueWrapper.value.int16Val );
                break;
            case SignalType::UINT32:
                signalProto.set_uint32_value( signalValueWrapper.value.uint32Val );
                break;
            case SignalType::INT32:
                signalProto.set_int32_value( signalValueWrapper.value.int32Val );
                break;
            case SignalType::UINT64:
                signalProto.set_uint64_value( signalValueWrapper.value.uint64Val );
                break;
            case SignalType::INT64:
                signalProto.set_int64_value( signalValueWrapper.value.int64Val );
                break;
            case SignalType::FLOAT:
                signalProto.set_float_value( signalValueWrapper.value.floatVal );
                break;
            case SignalType::DOUBLE:
                signalProto.set_double_value( signalValueWrapper.value.doubleVal );
                break;
            case SignalType::BOOLEAN:
                signalProto.set_boolean_value( signalValueWrapper.value.boolVal );
                break;
            default:
                FWE_LOG_ERROR(
                    "Skipping value for signal: " + std::to_string( signal.signalID ) +
                    " with unsupported type: " + std::to_string( static_cast<int>( signalValueWrapper.getType() ) ) );
                continue;
            }

            capturedSignalsProto->Add( std::move( signalProto ) );
            mMessageCount++;
            if ( mMessageCount >= mMaxMessagesPerPayload )
            {
                sendProto( logMessageIds, callback );
                resetProto( collectedData->triggerTime );
                logMessageIds.clear();
                capturedStateTemplateSignals = mProtoMsg.add_captured_state_template_signals();
                capturedStateTemplateSignals->set_state_template_sync_id(
                    stateTemplateCollectedSignals.stateTemplateSyncId );
                capturedSignalsProto = capturedStateTemplateSignals->mutable_captured_signals();
            }
        }
    }

    if ( mMessageCount > 0U )
    {
        sendProto( logMessageIds, callback );
    }
}

void
LastKnownStateDataSender::resetProto( Timestamp triggerTime )
{
    mMessageCount = 0;
    mProtoMsg.Clear();
    mProtoMsg.set_collection_event_time_ms_epoch( triggerTime );
}

void
LastKnownStateDataSender::sendProto( std::stringstream &logMessageIds,
                                     const Aws::IoTFleetWise::OnDataProcessedCallback &callback )
{
    auto protoOutput = std::make_shared<std::string>();

    // Note: a class member is used to store the serialized proto output to avoid heap fragmentation
    if ( !mProtoMsg.SerializeToString( protoOutput.get() ) )
    {
        FWE_LOG_ERROR( "Serialization failed for state template data with IDs: " + logMessageIds.str() );
        return;
    }

    auto compressedProtoOutput = std::make_shared<std::string>();
    if ( snappy::Compress( protoOutput->data(), protoOutput->size(), compressedProtoOutput.get() ) == 0U )
    {
        FWE_LOG_ERROR( "Data cannot be uploaded due to compression failure for state template data with IDs: " +
                       logMessageIds.str() );
        return;
    }
    protoOutput = compressedProtoOutput;

    mLastKnownStateSender->sendBuffer(
        mLastKnownStateSender->getTopicConfig().lastKnownStateDataTopic,
        reinterpret_cast<const uint8_t *>( protoOutput->data() ),
        protoOutput->size(),
        [stateTemplateSyncIds = logMessageIds.str(), payloadSize = protoOutput->size(), callback](
            ConnectivityError result ) {
            if ( result == ConnectivityError::Success )
            {
                FWE_LOG_INFO( "A LastKnownState payload of size: " + std::to_string( payloadSize ) +
                              " bytes has been uploaded for IDs: " + stateTemplateSyncIds );

                TraceModule::get().incrementVariable( TraceVariable::MQTT_LAST_KNOWN_STATE_MESSAGE_SENT_OUT );
                callback( true, nullptr );
            }
            else
            {
                FWE_LOG_ERROR( "Failed to send state template data with IDs: " + stateTemplateSyncIds +
                               " with error: " + std::to_string( static_cast<int>( result ) ) );
                TraceModule::get().incrementVariable( TraceVariable::MQTT_LAST_KNOWN_STATE_MESSAGE_FAILED_TO_BE_SENT );
                callback( false, nullptr );
            }
        } );
}

void
LastKnownStateDataSender::processPersistedData( std::istream &data,
                                                const Json::Value &metadata,
                                                OnPersistedDataProcessedCallback callback )
{
    static_cast<void>( data );
    static_cast<void>( metadata );
    static_cast<void>( callback );

    FWE_LOG_WARN( "Upload of persisted data is not supported for LastKnownState" );
}

} // namespace IoTFleetWise
} // namespace Aws
