// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateSchema.h"
#include "MqttClientWrapperMock.h"
#include "aws/iotfleetwise/AwsIotReceiver.h"
#include "aws/iotfleetwise/LastKnownStateIngestion.h"
#include "aws/iotfleetwise/LastKnownStateTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "state_templates.pb.h"
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Gt;
using ::testing::Return;
using ::testing::StrictMock;

class LastKnownStateSchemaTest : public ::testing::Test
{
protected:
    LastKnownStateSchemaTest()
        : mReceiverLastKnownState( mMqttClientWrapper, "topic" )
        , mLastKnownStateSchema( mReceiverLastKnownState )
    {
    }

    void
    SetUp() override
    {
        mLastKnownStateSchema.subscribeToLastKnownStateReceived(
            [&]( std::shared_ptr<LastKnownStateIngestion> lastKnownStateIngestion ) {
                mReceivedLastKnownStateIngestion = lastKnownStateIngestion;
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

    MqttClientBuilderWrapperMock mMqttClientBuilderWrapper;
    StrictMock<MqttClientWrapperMock> mMqttClientWrapper;
    AwsIotReceiver mReceiverLastKnownState;
    LastKnownStateSchema mLastKnownStateSchema;

    std::shared_ptr<LastKnownStateIngestion> mReceivedLastKnownStateIngestion;
};

TEST_F( LastKnownStateSchemaTest, ingestEmptyLastKnownState )
{
    std::string protoSerializedBuffer;

    mReceiverLastKnownState.onDataReceived( createPublishEvent( protoSerializedBuffer ) );

    // It is possible to receive an empty message, because an empty state template list in protobuf
    // would be just empty data.
    ASSERT_NE( mReceivedLastKnownStateIngestion, nullptr );
}

TEST_F( LastKnownStateSchemaTest, ingestLastKnownStateLargerThanLimit )
{
    std::string protoSerializedBuffer( LAST_KNOWN_STATE_BYTE_SIZE_LIMIT + 1, 'X' );

    mReceiverLastKnownState.onDataReceived( createPublishEvent( protoSerializedBuffer ) );

    ASSERT_EQ( mReceivedLastKnownStateIngestion, nullptr );
}

TEST_F( LastKnownStateSchemaTest, ingestLastKnownStateWithoutStateTemplates )
{
    Schemas::LastKnownState::StateTemplates protoLastKnownState;

    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoLastKnownState.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );
    mReceiverLastKnownState.onDataReceived( publishEvent );

    // This should be false because we just copied the data and it needs to be built first
    ASSERT_FALSE( mReceivedLastKnownStateIngestion->isReady() );

    ASSERT_FALSE( mReceivedLastKnownStateIngestion->build() );
    ASSERT_FALSE( mReceivedLastKnownStateIngestion->isReady() );
}

TEST_F( LastKnownStateSchemaTest, ingestLastKnownStateWithSignals )
{
    Schemas::LastKnownState::StateTemplates protoLastKnownState;
    protoLastKnownState.set_version( 456 );
    protoLastKnownState.set_decoder_manifest_sync_id( "decoder1" );
    auto *protoStateTemplateInfo = protoLastKnownState.add_state_templates_to_add();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->add_signal_ids( 1 );
    protoStateTemplateInfo->add_signal_ids( 2 );
    auto periodicUpdateStrategy = new Schemas::LastKnownState::PeriodicUpdateStrategy();
    periodicUpdateStrategy->set_period_ms( 2000 );
    protoStateTemplateInfo->set_allocated_periodic_update_strategy( periodicUpdateStrategy );

    protoStateTemplateInfo = protoLastKnownState.add_state_templates_to_add();
    protoStateTemplateInfo->set_state_template_sync_id( "lks2" );
    protoStateTemplateInfo->add_signal_ids( 3 );
    protoStateTemplateInfo->add_signal_ids( 4 );
    auto onChangeUpdateStrategy = new Schemas::LastKnownState::OnChangeUpdateStrategy();
    protoStateTemplateInfo->set_allocated_on_change_update_strategy( onChangeUpdateStrategy );

    protoLastKnownState.add_state_template_sync_ids_to_remove( "lks10" );
    protoLastKnownState.add_state_template_sync_ids_to_remove( "lks11" );

    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoLastKnownState.SerializeToString( &protoSerializedBuffer ) );

    auto publishEvent = createPublishEvent( protoSerializedBuffer );
    mReceiverLastKnownState.onDataReceived( publishEvent );

    ASSERT_TRUE( mReceivedLastKnownStateIngestion->build() );

    ASSERT_TRUE( mReceivedLastKnownStateIngestion->isReady() );
    auto stateTemplatesDiff = mReceivedLastKnownStateIngestion->getStateTemplatesDiff();

    ASSERT_EQ( stateTemplatesDiff->version, 456 );

    ASSERT_EQ( stateTemplatesDiff->stateTemplatesToRemove, std::vector<SyncID>( { "lks10", "lks11" } ) );

    auto &stateTemplatesToAdd = stateTemplatesDiff->stateTemplatesToAdd;
    ASSERT_EQ( stateTemplatesToAdd.size(), 2 );

    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->id, "lks1" );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->periodMs, 2000 );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->signals.size(), 2 );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->signals[0].signalID, 1 );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->signals[1].signalID, 2 );

    ASSERT_EQ( stateTemplatesToAdd.at( 1 )->id, "lks2" );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplatesToAdd.at( 1 )->updateStrategy, LastKnownStateUpdateStrategy::ON_CHANGE );
    ASSERT_EQ( stateTemplatesToAdd.at( 1 )->signals.size(), 2 );
    ASSERT_EQ( stateTemplatesToAdd.at( 1 )->signals[0].signalID, 3 );
    ASSERT_EQ( stateTemplatesToAdd.at( 1 )->signals[1].signalID, 4 );
}

TEST_F( LastKnownStateSchemaTest, ingestLastKnownStateWithInvalidDecoderManifest )
{
    Schemas::LastKnownState::StateTemplates protoLastKnownState;
    protoLastKnownState.set_version( 456 );
    // Empty decoder manifest ID. This message should fail to be processed.
    protoLastKnownState.set_decoder_manifest_sync_id( "" );
    auto *protoStateTemplateInfo = protoLastKnownState.add_state_templates_to_add();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->add_signal_ids( 1 );
    auto onChangeUpdateStrategy = new Schemas::LastKnownState::OnChangeUpdateStrategy();
    protoStateTemplateInfo->set_allocated_on_change_update_strategy( onChangeUpdateStrategy );

    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoLastKnownState.SerializeToString( &protoSerializedBuffer ) );

    mReceiverLastKnownState.onDataReceived( createPublishEvent( protoSerializedBuffer ) );

    ASSERT_FALSE( mReceivedLastKnownStateIngestion->build() );
    ASSERT_FALSE( mReceivedLastKnownStateIngestion->isReady() );
}

TEST_F( LastKnownStateSchemaTest, ingestLastKnownStateWithInvalidStateTemplate )
{
    Schemas::LastKnownState::StateTemplates protoLastKnownState;
    protoLastKnownState.set_version( 456 );
    protoLastKnownState.set_decoder_manifest_sync_id( "decoder1" );
    auto *protoStateTemplateInfo = protoLastKnownState.add_state_templates_to_add();
    protoStateTemplateInfo->set_state_template_sync_id( "lks1" );
    protoStateTemplateInfo->add_signal_ids( 1 );
    protoStateTemplateInfo->set_allocated_on_change_update_strategy(
        new Schemas::LastKnownState::OnChangeUpdateStrategy() );

    // No state template sync ID. This state template should be skipped.
    protoStateTemplateInfo = protoLastKnownState.add_state_templates_to_add();
    protoStateTemplateInfo->add_signal_ids( 2 );
    protoStateTemplateInfo->set_allocated_on_change_update_strategy(
        new Schemas::LastKnownState::OnChangeUpdateStrategy() );

    std::string protoSerializedBuffer;
    ASSERT_TRUE( protoLastKnownState.SerializeToString( &protoSerializedBuffer ) );

    mReceiverLastKnownState.onDataReceived( createPublishEvent( protoSerializedBuffer ) );

    ASSERT_TRUE( mReceivedLastKnownStateIngestion->build() );

    ASSERT_TRUE( mReceivedLastKnownStateIngestion->isReady() );
    auto stateTemplatesDiff = mReceivedLastKnownStateIngestion->getStateTemplatesDiff();

    ASSERT_EQ( stateTemplatesDiff->version, 456 );
    ASSERT_EQ( stateTemplatesDiff->stateTemplatesToRemove, std::vector<SyncID>() );

    auto &stateTemplatesToAdd = stateTemplatesDiff->stateTemplatesToAdd;
    ASSERT_EQ( stateTemplatesToAdd.size(), 1 );

    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->id, "lks1" );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->updateStrategy, LastKnownStateUpdateStrategy::ON_CHANGE );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->signals.size(), 1 );
    ASSERT_EQ( stateTemplatesToAdd.at( 0 )->signals[0].signalID, 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
