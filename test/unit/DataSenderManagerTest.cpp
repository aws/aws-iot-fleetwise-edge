// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManager.h"
#include "CANDataTypes.h"
#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "CollectionInspectionAPITypes.h"
#include "DataSenderProtoWriter.h"
#include "DataSenderTypes.h"
#include "IConnectionTypes.h"
#include "OBDDataTypes.h"
#include "PayloadManager.h"
#include "RawDataManager.h"
#include "SenderMock.h"
#include "SignalTypes.h"
#include "TelemetryDataSender.h"
#include "Testing.h"
#include "vehicle_data.pb.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <google/protobuf/message.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <snappy.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "CollectionSchemeManagerTest.h"
#include "DataSenderIonWriterMock.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "QueueTypes.h"
#include "S3Sender.h"
#include "S3SenderMock.h"
#include "StreambufBuilder.h"
#include "StringbufBuilder.h"
#include "VisionSystemDataSender.h"
#include <sstream>
#include <type_traits>
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
#include "CommandResponseDataSender.h"
#include "CommandTypes.h"
#include "ICommandDispatcher.h"
#include "command_response.pb.h"
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "LastKnownStateDataSender.h"
#include "LastKnownStateTypes.h"
#include "last_known_state_data.pb.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Gt;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;
using ::testing::WithArg;
using ::testing::WithArgs;

class DataSenderManagerTest : public ::testing::Test
{
public:
    void
    SetUp() override
    {
        std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
        mPersistency = createCacheAndPersist();
        mPayloadManager = std::make_shared<PayloadManager>( mPersistency );

        mTriggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();
        mTriggeredCollectionSchemeData->metadata.decoderID = "TESTDECODERID";
        mTriggeredCollectionSchemeData->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME";
        mTriggeredCollectionSchemeData->triggerTime = 1000000;
        mTriggeredCollectionSchemeData->eventID = 579;

        mCANIDTranslator.add( "can123" );

        mMqttSender = std::make_shared<StrictMock<Testing::SenderMock>>();
        EXPECT_CALL( *mMqttSender, isAlive() ).Times( AnyNumber() ).WillRepeatedly( Return( true ) );
        EXPECT_CALL( *mMqttSender, getMaxSendSize() )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( MAXIMUM_PAYLOAD_SIZE ) );
        ON_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) )
            .WillByDefault( InvokeArgument<3>( ConnectivityError::Success ) );

        auto mProtoWriter = std::make_shared<DataSenderProtoWriter>( mCANIDTranslator, mRawDataBufferManager );
        auto telemetryDataSender = std::make_shared<TelemetryDataSender>(
            mMqttSender, mProtoWriter, mPayloadAdaptionConfigUncompressed, mPayloadAdaptionConfigCompressed );
        std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders;
        dataSenders[SenderDataType::TELEMETRY] = std::move( telemetryDataSender );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        mTriggeredVisionSystemData = std::make_shared<TriggeredVisionSystemData>();
        mTriggeredVisionSystemData->metadata.decoderID = "TESTDECODERID";
        mTriggeredVisionSystemData->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME";
        mTriggeredVisionSystemData->triggerTime = 1000000;
        mTriggeredVisionSystemData->eventID = 579;

        mS3Sender = std::make_shared<StrictMock<Testing::S3SenderMock>>();
        mIonWriter = std::make_shared<StrictMock<Testing::DataSenderIonWriterMock>>();
        mUploadedS3Objects = std::make_shared<DataSenderQueue>( 100, "Uploaded S3 Objects" );
        mActiveCollectionSchemes = std::make_shared<ActiveCollectionSchemes>();
        mVisionSystemDataSender =
            std::make_shared<VisionSystemDataSender>( mUploadedS3Objects, mS3Sender, mIonWriter, "" );
        dataSenders[SenderDataType::VISION_SYSTEM] = mVisionSystemDataSender;
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
        mCommandResponseSender = std::make_shared<StrictMock<Testing::SenderMock>>();
        EXPECT_CALL( *mCommandResponseSender, isAlive() ).Times( AnyNumber() ).WillRepeatedly( Return( true ) );
        ON_CALL( *mCommandResponseSender, mockedSendBuffer( _, _, _, _ ) )
            .WillByDefault( InvokeArgument<3>( ConnectivityError::Success ) );
        mCommandResponseDataSender = std::make_shared<CommandResponseDataSender>( mCommandResponseSender );
        dataSenders[SenderDataType::COMMAND_RESPONSE] = mCommandResponseDataSender;
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        mLastKnownStateMqttSender = std::make_shared<StrictMock<Testing::SenderMock>>();
        ON_CALL( *mLastKnownStateMqttSender, mockedSendBuffer( mLastKnownStateDataTopic, _, _, _ ) )
            .WillByDefault( InvokeArgument<3>( ConnectivityError::Success ) );
        mLastKnownStateDataSender =
            std::make_shared<LastKnownStateDataSender>( mLastKnownStateMqttSender, mMaxMessagesPerPayload );
        dataSenders[SenderDataType::LAST_KNOWN_STATE] = mLastKnownStateDataSender;
#endif

        mDataSenderManager =
            std::make_unique<DataSenderManager>( std::move( dataSenders ), mMqttSender, mPayloadManager );
    }

    void
    TearDown() override
    {
    }

    void
    processCollectedData( std::shared_ptr<DataToSend> data )
    {
        mDataSenderManager->processData( std::const_pointer_cast<const DataToSend>( data ) );
    }

#ifdef FWE_FEATURE_REMOTE_COMMANDS
    std::string
    getCommandResponseTopic( const std::string &commandID ) const
    {
        return "$aws/commands/things/thing-name/executions/" + commandID + "/response/protobuf";
    }
#endif

protected:
    static constexpr unsigned MAXIMUM_PAYLOAD_SIZE = 400;
    static constexpr unsigned CAN_DATA_SIZE = 8;
    PayloadAdaptionConfig mPayloadAdaptionConfigUncompressed{ 80, 70, 90, 10 };
    PayloadAdaptionConfig mPayloadAdaptionConfigCompressed{ 80, 70, 90, 10 };
    unsigned mMaxMessagesPerPayload{ 5 }; // Only used by LastKnownStateDataSender

    unsigned mCanChannelID{ 0 };
    std::shared_ptr<TriggeredCollectionSchemeData> mTriggeredCollectionSchemeData;
    std::string mTelemetryDataTopic = "$aws/iotfleetwise/vehicles/thing-name/signals";
    std::shared_ptr<StrictMock<Testing::SenderMock>> mMqttSender;
    std::shared_ptr<CacheAndPersist> mPersistency;
    std::shared_ptr<PayloadManager> mPayloadManager;
    CANInterfaceIDTranslator mCANIDTranslator;
    std::unique_ptr<DataSenderManager> mDataSenderManager;
    std::shared_ptr<DataSenderProtoWriter> mProtoWriter;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::shared_ptr<TriggeredVisionSystemData> mTriggeredVisionSystemData;
    std::shared_ptr<VisionSystemDataSender> mVisionSystemDataSender;
    std::shared_ptr<StrictMock<Testing::S3SenderMock>> mS3Sender;
    std::shared_ptr<StrictMock<Testing::DataSenderIonWriterMock>> mIonWriter;
    std::shared_ptr<ActiveCollectionSchemes> mActiveCollectionSchemes;
    std::shared_ptr<DataSenderQueue> mUploadedS3Objects;
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    std::shared_ptr<StrictMock<Testing::SenderMock>> mCommandResponseSender;
    std::shared_ptr<CommandResponseDataSender> mCommandResponseDataSender;
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    std::string mLastKnownStateDataTopic = "$aws/iotfleetwise/vehicles/thing-name/last_known_states/data";
    std::shared_ptr<StrictMock<Testing::SenderMock>> mLastKnownStateMqttSender;
    std::shared_ptr<LastKnownStateDataSender> mLastKnownStateDataSender;
#endif
};

template <typename T>
std::vector<double>
getSignalValues( const T &signals )
{
    std::vector<double> signalValues;
    for ( const auto &signal : signals )
    {
        signalValues.push_back( signal.double_value() );
    }
    return signalValues;
}

template <typename T>
std::vector<double>
getSignalIds( const T &signals )
{
    std::vector<double> signalIds;
    for ( const auto &signal : signals )
    {
        signalIds.push_back( signal.signal_id() );
    }
    return signalIds;
}

TEST_F( DataSenderManagerTest, senderDataTypeToString )
{
    EXPECT_EQ( senderDataTypeToString( SenderDataType::TELEMETRY ), "Telemetry" );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    EXPECT_EQ( senderDataTypeToString( SenderDataType::VISION_SYSTEM ), "VisionSystem" );
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    EXPECT_EQ( senderDataTypeToString( SenderDataType::COMMAND_RESPONSE ), "CommandResponse" );
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    EXPECT_EQ( senderDataTypeToString( SenderDataType::LAST_KNOWN_STATE ), "LastKnownState" );
#endif
    EXPECT_EQ( senderDataTypeToString( static_cast<SenderDataType>( -1 ) ), "" );
}

TEST_F( DataSenderManagerTest, stringToSenderDataType )
{
    SenderDataType output;
    EXPECT_TRUE( stringToSenderDataType( "Telemetry", output ) );
    EXPECT_EQ( output, SenderDataType::TELEMETRY );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    EXPECT_TRUE( stringToSenderDataType( "VisionSystem", output ) );
    EXPECT_EQ( output, SenderDataType::VISION_SYSTEM );
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    EXPECT_TRUE( stringToSenderDataType( "CommandResponse", output ) );
    EXPECT_EQ( output, SenderDataType::COMMAND_RESPONSE );
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    EXPECT_TRUE( stringToSenderDataType( "LastKnownState", output ) );
    EXPECT_EQ( output, SenderDataType::LAST_KNOWN_STATE );
#endif
    EXPECT_FALSE( stringToSenderDataType( "Invalid", output ) );
}

TEST_F( DataSenderManagerTest, ProcessEmptyData )
{
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );

    // It should just not crash
    processCollectedData( nullptr );
}

TEST_F( DataSenderManagerTest, ProcessSingleSignal )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );
}

TEST_F( DataSenderManagerTest, ProcessMultipleSignals )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    auto signal2 = CollectedSignal( 1234, 789700, 42.5, SignalType::DOUBLE );
    auto signal3 = CollectedSignal( 5678, 789980, 22.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );
    mTriggeredCollectionSchemeData->signals.push_back( signal2 );
    mTriggeredCollectionSchemeData->signals.push_back( signal3 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 3 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );
    ASSERT_EQ( vehicleData.captured_signals()[1].signal_id(), signal2.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[1].double_value(), signal2.value.value.doubleVal );
    ASSERT_EQ( vehicleData.captured_signals()[2].signal_id(), signal3.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[2].double_value(), signal3.value.value.doubleVal );
}

TEST_F( DataSenderManagerTest, ProcessMultipleSignalsBeyondTransmitThreshold )
{
    std::vector<CollectedSignal> signals = { CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ),
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ) };
    mTriggeredCollectionSchemeData->signals = signals;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( InvokeArgument<3>( ConnectivityError::Success ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    EXPECT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    EXPECT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    EXPECT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    EXPECT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 14 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    EXPECT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    EXPECT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    EXPECT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    EXPECT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 4 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
}

TEST_F( DataSenderManagerTest, ProcessSingleCanFrame )
{
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame1 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE );
    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 1 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.can_frames()[0].message_id(), canFrame1.frameID );
    ASSERT_EQ( vehicleData.can_frames()[0].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[0].byte_values(),
               std::string( canBuf1.begin(), canBuf1.begin() + CAN_DATA_SIZE ) );
}

TEST_F( DataSenderManagerTest, ProcessMultipleCanFrames )
{
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame1 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf2 = { 0xBA, 0xAD, 0xAF, 0xFE, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame2 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf2, CAN_DATA_SIZE );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf3 = { 0xCA, 0xFE, 0xF0, 0x0D, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame3 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf3, CAN_DATA_SIZE );

    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame1 );
    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame2 );
    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame3 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 3 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.can_frames()[0].message_id(), canFrame1.frameID );
    ASSERT_EQ( vehicleData.can_frames()[0].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[0].byte_values(),
               std::string( canBuf1.begin(), canBuf1.begin() + CAN_DATA_SIZE ) );
    ASSERT_EQ( vehicleData.can_frames()[1].message_id(), canFrame2.frameID );
    ASSERT_EQ( vehicleData.can_frames()[1].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[1].byte_values(),
               std::string( canBuf2.begin(), canBuf2.begin() + CAN_DATA_SIZE ) );
    ASSERT_EQ( vehicleData.can_frames()[2].message_id(), canFrame3.frameID );
    ASSERT_EQ( vehicleData.can_frames()[2].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[2].byte_values(),
               std::string( canBuf3.begin(), canBuf3.begin() + CAN_DATA_SIZE ) );
}

TEST_F( DataSenderManagerTest, ProcessMultipleCanFramesBeyondTransmitThreshold )
{
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    std::vector<CollectedCanRawFrame> canFrames = {
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, CAN_DATA_SIZE ) };
    mTriggeredCollectionSchemeData->canFrames = canFrames;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 2 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 10 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 5 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
}

TEST_F( DataSenderManagerTest, ProcessSingleDtcCode )
{
    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = 789654;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    mTriggeredCollectionSchemeData->mDTCInfo = dtcInfo;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 1 );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes()[0], "P0143" );
}

TEST_F( DataSenderManagerTest, ProcessMultipleDtcCodes )
{
    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = 789654;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    dtcInfo.mDTCCodes.emplace_back( "C0196" );
    mTriggeredCollectionSchemeData->mDTCInfo = dtcInfo;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 2 );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes()[0], "P0143" );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes()[1], "C0196" );
}

TEST_F( DataSenderManagerTest, ProcessMultipleDtcCodesBeyondTransmitThreshold )
{
    std::vector<std::string> dtcCodes = { "P0143_________________________",
                                          "C0196_________________________",
                                          "U0148_________________________",
                                          "B0148_________________________",
                                          "C0148_________________________",
                                          "C0149_________________________",
                                          "C0150_________________________",
                                          "C0151_________________________",
                                          "C0152_________________________",
                                          "C0153_________________________" };

    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = 789654;
    dtcInfo.mDTCCodes = dtcCodes;
    mTriggeredCollectionSchemeData->mDTCInfo = dtcInfo;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 2 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 9 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 1 );
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
TEST_F( DataSenderManagerTest, ProcessSingleUploadedS3Object )
{
    auto uploadedS3Object1 = UploadedS3Object{ "uploaded/object/key1", UploadedS3ObjectDataFormat::Cdr };
    mTriggeredCollectionSchemeData->uploadedS3Objects.push_back( uploadedS3Object1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_EQ( vehicleData.s3_objects_size(), 1 );

    ASSERT_EQ( vehicleData.s3_objects()[0].key(), "uploaded/object/key1" );
    ASSERT_EQ( vehicleData.s3_objects()[0].data_format(), Schemas::VehicleDataMsg::DataFormat::CDR );
}

TEST_F( DataSenderManagerTest, ProcessMultipleUploadedS3Objects )
{
    auto uploadedS3Object1 = UploadedS3Object{ "uploaded/object/key1", UploadedS3ObjectDataFormat::Cdr };
    auto uploadedS3Object2 = UploadedS3Object{ "uploaded/object/key2", UploadedS3ObjectDataFormat::Unknown };
    auto uploadedS3Object3 = UploadedS3Object{ "uploaded/object/key3", UploadedS3ObjectDataFormat::Cdr };
    mTriggeredCollectionSchemeData->uploadedS3Objects.push_back( uploadedS3Object1 );
    mTriggeredCollectionSchemeData->uploadedS3Objects.push_back( uploadedS3Object2 );
    mTriggeredCollectionSchemeData->uploadedS3Objects.push_back( uploadedS3Object3 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_EQ( vehicleData.s3_objects_size(), 3 );

    ASSERT_EQ( vehicleData.s3_objects()[0].key(), "uploaded/object/key1" );
    ASSERT_EQ( vehicleData.s3_objects()[0].data_format(), Schemas::VehicleDataMsg::DataFormat::CDR );
    ASSERT_EQ( vehicleData.s3_objects()[1].key(), "uploaded/object/key2" );
    ASSERT_EQ( vehicleData.s3_objects()[1].data_format(), Schemas::VehicleDataMsg::DataFormat::UNKNOWN_DATA_FORMAT );
    ASSERT_EQ( vehicleData.s3_objects()[2].key(), "uploaded/object/key3" );
    ASSERT_EQ( vehicleData.s3_objects()[2].data_format(), Schemas::VehicleDataMsg::DataFormat::CDR );
}

TEST_F( DataSenderManagerTest, ProcessMultipleUploadedS3ObjectsBeyondTransmitThreshold )
{
    std::vector<UploadedS3Object> uploadedS3Objects = {
        UploadedS3Object{ "uploaded/object/key1", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key2", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key3", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key4", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key5", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key6", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key7", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key8", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key9", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key9", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key9", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key9", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key9", UploadedS3ObjectDataFormat::Cdr },
        UploadedS3Object{ "uploaded/object/key9", UploadedS3ObjectDataFormat::Cdr },
    };
    mTriggeredCollectionSchemeData->uploadedS3Objects = uploadedS3Objects;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 2 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_EQ( vehicleData.s3_objects_size(), 11 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_EQ( vehicleData.s3_objects_size(), 3 );
}

TEST_F( DataSenderManagerTest, ProcessRawDataSignalNoActiveCampaigns )
{
    auto signal1 = CollectedSignal( 1234, 789654, 10000, SignalType::COMPLEX_SIGNAL );
    mTriggeredVisionSystemData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).Times( 0 );

    processCollectedData( mTriggeredVisionSystemData );
}

TEST_F( DataSenderManagerTest, ProcessVisionSystemDataWithoutRawData )
{
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).Times( 0 );

    processCollectedData( mTriggeredVisionSystemData );
}

TEST_F( DataSenderManagerTest, ProcessSingleRawDataSignal )
{
    auto signal1 = CollectedSignal( 1234, 789654, 888999, SignalType::COMPLEX_SIGNAL );
    mTriggeredVisionSystemData->signals.push_back( signal1 );

    auto s3UploadMetadata = S3UploadMetadata();
    s3UploadMetadata.bucketName = "BucketName";
    s3UploadMetadata.bucketOwner = "1234567890";
    s3UploadMetadata.prefix = "s3/prefix/raw-data/";
    s3UploadMetadata.region = "eu-central-1";
    ICollectionSchemePtr collectionScheme1 =
        std::make_shared<ICollectionSchemeTest>( "TESTCOLLECTIONSCHEME", "TESTDECODERID", 0, 10, s3UploadMetadata );
    mActiveCollectionSchemes->activeCollectionSchemes.push_back( collectionScheme1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).WillOnce( []() {
        return std::move( std::make_unique<Testing::StringbufBuilder>( "fake ion file" ) );
    } );

    std::unique_ptr<std::streambuf> sentStream;
    EXPECT_CALL( *mS3Sender, sendStream( _, s3UploadMetadata, std::string( "s3/prefix/raw-data/579-1000000.10n" ), _ ) )
        .WillOnce( WithArgs<0, 3>( [&sentStream]( std::unique_ptr<StreambufBuilder> streambufBuilder,
                                                  S3Sender::ResultCallback resultCallback ) {
            sentStream = std::move( streambufBuilder->build() );
            resultCallback( ConnectivityError::Success, nullptr );
        } ) );

    mVisionSystemDataSender->onChangeCollectionSchemeList( mActiveCollectionSchemes );
    processCollectedData( mTriggeredVisionSystemData );

    ASSERT_EQ( mIonWriter->mSignals.size(), 1 );
    ASSERT_EQ( mIonWriter->mSignals[0].signalID, 1234 );
    ASSERT_EQ( mIonWriter->mSignals[0].value.value.uint32Val, 888999U );

    std::shared_ptr<const DataToSend> senderData;
    ASSERT_TRUE( mUploadedS3Objects->pop( senderData ) );
    auto reportedCollectionSchemeData = std::dynamic_pointer_cast<const TriggeredCollectionSchemeData>( senderData );
    ASSERT_NE( reportedCollectionSchemeData, nullptr );
    ASSERT_EQ( reportedCollectionSchemeData->eventID, mTriggeredVisionSystemData->eventID );
    ASSERT_EQ( reportedCollectionSchemeData->triggerTime, mTriggeredVisionSystemData->triggerTime );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.compress, mTriggeredVisionSystemData->metadata.compress );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.persist, mTriggeredVisionSystemData->metadata.persist );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.collectionSchemeID,
               mTriggeredVisionSystemData->metadata.collectionSchemeID );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.decoderID, mTriggeredVisionSystemData->metadata.decoderID );

    ASSERT_FALSE( reportedCollectionSchemeData->mDTCInfo.hasItems() );
    ASSERT_EQ( reportedCollectionSchemeData->signals.size(), 0 );
    ASSERT_EQ( reportedCollectionSchemeData->canFrames.size(), 0 );
    ASSERT_EQ( reportedCollectionSchemeData->uploadedS3Objects.size(), 1 );

    {
        std::ostringstream sentFile;
        sentFile << &( *sentStream );
        ASSERT_EQ( sentFile.str(), "fake ion file" );
    }
}

TEST_F( DataSenderManagerTest, ProcessMultipleRawDataSignals )
{
    auto signal1 = CollectedSignal( 1234, 789654, 888999, SignalType::COMPLEX_SIGNAL );
    mTriggeredVisionSystemData->signals.push_back( signal1 );
    auto signal2 = CollectedSignal( 5678, 789987, 889000, SignalType::COMPLEX_SIGNAL );
    mTriggeredVisionSystemData->signals.push_back( signal2 );

    auto s3UploadMetadata = S3UploadMetadata();
    s3UploadMetadata.bucketName = "BucketName";
    s3UploadMetadata.bucketOwner = "1234567890";
    s3UploadMetadata.prefix = "s3/prefix/raw-data/";
    s3UploadMetadata.region = "eu-central-1";
    ICollectionSchemePtr collectionScheme1 =
        std::make_shared<ICollectionSchemeTest>( "TESTCOLLECTIONSCHEME", "TESTDECODERID", 0, 10, s3UploadMetadata );
    mActiveCollectionSchemes->activeCollectionSchemes.push_back( collectionScheme1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 2 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).WillOnce( []() {
        return std::move( std::make_unique<Testing::StringbufBuilder>( "fake ion file" ) );
    } );
    EXPECT_CALL( *mS3Sender, sendStream( _, s3UploadMetadata, std::string( "s3/prefix/raw-data/579-1000000.10n" ), _ ) )
        // Can't use DoAll(InvokeArgument, Return) here: https://stackoverflow.com/a/70886530
        .WillOnce( WithArg<3>( []( S3Sender::ResultCallback resultCallback ) {
            resultCallback( ConnectivityError::Success, nullptr );
        } ) );

    mVisionSystemDataSender->onChangeCollectionSchemeList( mActiveCollectionSchemes );
    processCollectedData( mTriggeredVisionSystemData );

    ASSERT_EQ( mIonWriter->mSignals.size(), 2 );
    ASSERT_EQ( mIonWriter->mSignals[0].signalID, 1234 );
    ASSERT_EQ( mIonWriter->mSignals[0].value.value.uint32Val, 888999U );
    ASSERT_EQ( mIonWriter->mSignals[1].signalID, 5678 );
    ASSERT_EQ( mIonWriter->mSignals[1].value.value.uint32Val, 889000U );

    std::shared_ptr<const DataToSend> senderData;
    ASSERT_TRUE( mUploadedS3Objects->pop( senderData ) );
    auto reportedCollectionSchemeData = std::dynamic_pointer_cast<const TriggeredCollectionSchemeData>( senderData );
    ASSERT_NE( reportedCollectionSchemeData, nullptr );
    ASSERT_EQ( reportedCollectionSchemeData->eventID, mTriggeredVisionSystemData->eventID );
    ASSERT_EQ( reportedCollectionSchemeData->triggerTime, mTriggeredVisionSystemData->triggerTime );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.compress, mTriggeredVisionSystemData->metadata.compress );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.persist, mTriggeredVisionSystemData->metadata.persist );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.collectionSchemeID,
               mTriggeredVisionSystemData->metadata.collectionSchemeID );
    ASSERT_EQ( reportedCollectionSchemeData->metadata.decoderID, mTriggeredVisionSystemData->metadata.decoderID );

    ASSERT_FALSE( reportedCollectionSchemeData->mDTCInfo.hasItems() );
    ASSERT_EQ( reportedCollectionSchemeData->signals.size(), 0 );
    ASSERT_EQ( reportedCollectionSchemeData->canFrames.size(), 0 );
    ASSERT_EQ( reportedCollectionSchemeData->uploadedS3Objects.size(), 1 );
}

TEST_F( DataSenderManagerTest, ProcessRawDataSignalFailure )
{
    auto signal1 = CollectedSignal( 1234, 789654, 888999, SignalType::COMPLEX_SIGNAL );
    mTriggeredVisionSystemData->signals.push_back( signal1 );

    auto s3UploadMetadata = S3UploadMetadata();
    s3UploadMetadata.bucketName = "BucketName";
    s3UploadMetadata.bucketOwner = "1234567890";
    s3UploadMetadata.prefix = "s3/prefix/raw-data/";
    s3UploadMetadata.region = "eu-central-1";
    ICollectionSchemePtr collectionScheme1 =
        std::make_shared<ICollectionSchemeTest>( "TESTCOLLECTIONSCHEME", "TESTDECODERID", 0, 10, s3UploadMetadata );
    mActiveCollectionSchemes->activeCollectionSchemes.push_back( collectionScheme1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).WillOnce( []() {
        return std::move( std::make_unique<Testing::StringbufBuilder>( "fake ion file" ) );
    } );
    EXPECT_CALL( *mS3Sender, sendStream( _, s3UploadMetadata, std::string( "s3/prefix/raw-data/579-1000000.10n" ), _ ) )
        // Can't use DoAll(InvokeArgument, Return) here: https://stackoverflow.com/a/70886530
        .WillOnce( WithArg<3>( []( S3Sender::ResultCallback resultCallback ) {
            resultCallback( ConnectivityError::TransmissionError, nullptr );
        } ) );

    mVisionSystemDataSender->onChangeCollectionSchemeList( mActiveCollectionSchemes );
    processCollectedData( mTriggeredVisionSystemData );

    ASSERT_EQ( mIonWriter->mSignals.size(), 1 );
    ASSERT_EQ( mIonWriter->mSignals[0].signalID, 1234 );
    ASSERT_EQ( mIonWriter->mSignals[0].value.value.uint32Val, 888999U );

    std::shared_ptr<const DataToSend> senderData;
    ASSERT_FALSE( mUploadedS3Objects->pop( senderData ) );
}

TEST_F( DataSenderManagerTest, ProcessRawDataSignalFailureWithPersistency )
{
    mTriggeredVisionSystemData->metadata.persist = true;

    auto signal1 = CollectedSignal( 1234, 789654, 888999, SignalType::COMPLEX_SIGNAL );
    mTriggeredVisionSystemData->signals.push_back( signal1 );

    auto s3UploadMetadata = S3UploadMetadata();
    s3UploadMetadata.bucketName = "BucketName";
    s3UploadMetadata.bucketOwner = "1234567890";
    s3UploadMetadata.prefix = "s3/prefix/raw-data/";
    s3UploadMetadata.region = "eu-central-1";
    ICollectionSchemePtr collectionScheme1 =
        std::make_shared<ICollectionSchemeTest>( "TESTCOLLECTIONSCHEME", "TESTDECODERID", 0, 10, s3UploadMetadata );
    mActiveCollectionSchemes->activeCollectionSchemes.push_back( collectionScheme1 );
    std::string payload = "fake ion file";

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).WillOnce( [payload]() {
        return std::move( std::make_unique<Testing::StringbufBuilder>( payload ) );
    } );
    EXPECT_CALL( *mS3Sender, sendStream( _, s3UploadMetadata, std::string( "s3/prefix/raw-data/579-1000000.10n" ), _ ) )
        // Can't use DoAll(InvokeArgument, Return) here: https://stackoverflow.com/a/70886530
        .WillOnce( WithArg<3>( [payload]( S3Sender::ResultCallback resultCallback ) {
            // This should make the file to be persisted
            resultCallback( ConnectivityError::TransmissionError,
                            std::make_unique<Testing::StringbufBuilder>( payload )->build() );
        } ) );

    mVisionSystemDataSender->onChangeCollectionSchemeList( mActiveCollectionSchemes );
    processCollectedData( mTriggeredVisionSystemData );

    ASSERT_EQ( mIonWriter->mSignals.size(), 1 );
    ASSERT_EQ( mIonWriter->mSignals[0].signalID, 1234 );
    ASSERT_EQ( mIonWriter->mSignals[0].value.value.uint32Val, 888999U );

    std::shared_ptr<const DataToSend> senderData;
    ASSERT_FALSE( mUploadedS3Objects->pop( senderData ) );

    std::vector<uint8_t> fileContent( payload.size() );
    ASSERT_EQ(
        mPayloadManager->retrievePayload( fileContent.data(), payload.size(), "s3/prefix/raw-data/579-1000000.10n" ),
        ErrorCode::SUCCESS );
    ASSERT_EQ( std::string( fileContent.begin(), fileContent.end() ), payload );
}

TEST_F( DataSenderManagerTest, ProcessSingleSignalWithoutRawData )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    // CollectionScheme is configured for S3 upload but there is no RawData available. So no Ion file
    // should be created and nothing should be uploaded to S3.
    auto s3UploadMetadata = S3UploadMetadata();
    s3UploadMetadata.bucketName = "BucketName";
    s3UploadMetadata.bucketOwner = "1234567890";
    s3UploadMetadata.prefix = "s3/prefix/raw-data/";
    s3UploadMetadata.region = "eu-central-1";
    ICollectionSchemePtr collectionScheme1 =
        std::make_shared<ICollectionSchemeTest>( "TESTCOLLECTIONSCHEME", "TESTDECODERID", 0, 10, s3UploadMetadata );
    mActiveCollectionSchemes->activeCollectionSchemes.push_back( collectionScheme1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );
    EXPECT_CALL( *mS3Sender, sendStream( _, _, _, _ ) ).Times( 0 );

    mVisionSystemDataSender->onChangeCollectionSchemeList( mActiveCollectionSchemes );
    processCollectedData( mTriggeredCollectionSchemeData );

    std::shared_ptr<const DataToSend> senderData;
    ASSERT_FALSE( mUploadedS3Objects->pop( senderData ) );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
}
#endif

TEST_F( DataSenderManagerTest, ProcessSingleSignalWithCompression )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );
    mTriggeredCollectionSchemeData->metadata.compress = true;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    std::string uncompressedData;
    snappy::Uncompress( sentBufferData[0].data.c_str(), sentBufferData[0].data.size(), &uncompressedData );
    ASSERT_TRUE( vehicleData.ParseFromString( uncompressedData ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );
}

TEST_F( DataSenderManagerTest, PersistencyNoFiles )
{
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );

    mDataSenderManager->checkAndSendRetrievedData();
}

TEST_F( DataSenderManagerTest, PersistencyUnsupportedFile )
{
    std::string filename = "filename1.bin";
    std::string payload = "fake collection payload";

    Json::Value payloadMetadata;
    payloadMetadata["filename"] = filename;
    payloadMetadata["payloadSize"] = payload.size();

    Json::Value metadata;
    metadata["type"] = "SomeUnsupportedType";
    metadata["payload"] = payloadMetadata;

    mPayloadManager->storeData(
        reinterpret_cast<const uint8_t *>( payload.data() ), payload.size(), metadata, filename );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );

    mDataSenderManager->checkAndSendRetrievedData();
}

TEST_F( DataSenderManagerTest, PersistencyMissingPayloadFile )
{
    std::string filename = "filename1.bin";

    Json::Value payloadMetadata;
    payloadMetadata["filename"] = filename;
    payloadMetadata["payloadSize"] = 10;

    Json::Value metadata;
    metadata["type"] = senderDataTypeToString( SenderDataType::TELEMETRY );
    metadata["payload"] = payloadMetadata;

    mPersistency->addMetadata( metadata );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) ).Times( 0 );

    mDataSenderManager->checkAndSendRetrievedData();
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetryDisabled )
{
    mTriggeredCollectionSchemeData->metadata.persist = false;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetryLegacyMetadata )
{
    std::string filename = "12345-1234567890.bin";
    std::string payload = "fake collection payload";

    Json::Value metadata;
    metadata["filename"] = filename;
    metadata["payloadSize"] = payload.size();
    metadata["compressionRequired"] = true;

    mPayloadManager->storeData(
        reinterpret_cast<const uint8_t *>( payload.data() ), payload.size(), metadata, filename );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    ASSERT_EQ( sentBufferData[0].data, payload );

    // Ensure that there is no more data persisted
    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetrySingleFile )
{
    mTriggeredCollectionSchemeData->metadata.persist = true;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );

    // Ensure that there is no more data persisted
    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyKeepFilesWhenRestarting )
{
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    mTriggeredCollectionSchemeData->metadata.persist = true;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    // Now simulate a system restart
    mDataSenderManager.reset();
    mPayloadManager.reset();
    mPersistency.reset();
    mPersistency = createCacheAndPersist();
    mPayloadManager = std::make_shared<PayloadManager>( mPersistency );
    std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders = {
        { SenderDataType::TELEMETRY,
          std::make_shared<TelemetryDataSender>(
              mMqttSender, mProtoWriter, mPayloadAdaptionConfigUncompressed, mPayloadAdaptionConfigCompressed ) } };
    mDataSenderManager = std::make_unique<DataSenderManager>( dataSenders, mMqttSender, mPayloadManager );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );

    // Ensure that there is no more data persisted
    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetryPersistAgainOnFailure )
{
    mTriggeredCollectionSchemeData->metadata.persist = true;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::NoConnection ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );

    mDataSenderManager->checkAndSendRetrievedData();

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );

    // Now the next attempt succeeds
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    ASSERT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );

    // Ensure that there is no more data persisted
    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetryMultipleFiles )
{
    mTriggeredCollectionSchemeData->metadata.persist = true;

    // The following should trigger 2 uploads, which will then be 2 files when persisting
    int numberOfSignals;
    size_t estimatedSize = 2 * ( mTriggeredCollectionSchemeData->metadata.collectionSchemeID.size() +
                                 mTriggeredCollectionSchemeData->metadata.decoderID.size() + 12 );
    size_t maxSize = 2 * MAXIMUM_PAYLOAD_SIZE * mPayloadAdaptionConfigUncompressed.transmitThresholdStartPercent / 100;
    for ( numberOfSignals = 0; ( estimatedSize + 20 ) < maxSize; numberOfSignals++ )
    {
        auto signal = CollectedSignal( 1234, 700000 + numberOfSignals, numberOfSignals, SignalType::DOUBLE );
        mTriggeredCollectionSchemeData->signals.push_back( signal );
        estimatedSize += 20;
    }

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( InvokeArgument<3>( ConnectivityError::NoConnection ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( InvokeArgument<3>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    EXPECT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    EXPECT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    EXPECT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    EXPECT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    auto firstSize = vehicleData.captured_signals_size();
    ASSERT_TRUE( ( firstSize == ( numberOfSignals / 2 ) ) || ( firstSize == ( ( numberOfSignals / 2 ) + 1 ) ) );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    for ( int i = 0; i < vehicleData.captured_signals_size(); i++ )
    {
        EXPECT_EQ( vehicleData.captured_signals()[i].signal_id(), 1234 );
        EXPECT_EQ( vehicleData.captured_signals()[i].double_value(), i );
    }

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    EXPECT_EQ( vehicleData.collection_event_id(), mTriggeredCollectionSchemeData->eventID );
    EXPECT_EQ( vehicleData.collection_event_time_ms_epoch(), mTriggeredCollectionSchemeData->triggerTime );
    EXPECT_EQ( vehicleData.campaign_sync_id(), mTriggeredCollectionSchemeData->metadata.collectionSchemeID );
    EXPECT_EQ( vehicleData.decoder_sync_id(), mTriggeredCollectionSchemeData->metadata.decoderID );

    ASSERT_EQ( vehicleData.captured_signals_size(), numberOfSignals - firstSize );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );

    for ( int i = 0; i < vehicleData.captured_signals_size(); i++ )
    {
        EXPECT_EQ( vehicleData.captured_signals()[i].signal_id(), 1234 );
        EXPECT_EQ( vehicleData.captured_signals()[i].double_value(), i + firstSize );
    }

    // Ensure that there is no more data persisted
    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, splitAndDecreaseThresholdWhenOverLimit )
{
    // Change the maximum payload size, so that a payload will be built that doesn't fit, causing the transmit threshold
    // to be iteratively decreased. The payloads are split, with the first few being dropped as splitting into quarters
    // is still too big.
    EXPECT_CALL( *mMqttSender, getMaxSendSize() ).Times( AnyNumber() ).WillRepeatedly( Return( 95 ) );

    for ( auto i = 0; i < 100; i++ )
    {
        mTriggeredCollectionSchemeData->signals.push_back( CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ) );
    }

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .Times( AtLeast( 12 ) )
        .WillRepeatedly( InvokeArgument<3>( ConnectivityError::Success ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_GE( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 12 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;

    // Split into quarters fails. Threshold is decreased.

    // Split into quarters. Threshold is decreased.
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[2].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[3].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );

    // Split into halves. Threshold is decreased.
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[4].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[5].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );

    // Split into halves. Threshold is decreased.
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[6].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[7].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );

    // Threshold now constant:
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[8].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[9].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[10].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[11].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 2 );
}

TEST_F( DataSenderManagerTest, increaseThresholdWhenBelowLimit )
{
    // Change the maximum payload size, so the payload size is under the maximum limit, causing the transmit threshold
    // to be iteratively increased:
    EXPECT_CALL( *mMqttSender, getMaxSendSize() )
        .Times( AnyNumber() )
        .WillRepeatedly( Return( MAXIMUM_PAYLOAD_SIZE * 2 ) );

    for ( auto i = 0; i < 200; i++ )
    {
        mTriggeredCollectionSchemeData->signals.push_back( CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ) );
    }

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
        .Times( AtLeast( 9 ) )
        .WillRepeatedly( InvokeArgument<3>( ConnectivityError::Success ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_GE( mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 9 );
    auto sentBufferData = mMqttSender->getSentBufferDataByTopic( mTelemetryDataTopic );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    // Increasing threshold:
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 14 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 16 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[2].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 17 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[3].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 19 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[4].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 21 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[5].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 24 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[6].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 26 );

    // Threshold now constant:
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[7].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 29 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[8].data ) );
    EXPECT_EQ( vehicleData.captured_signals_size(), 29 );
}

#ifdef FWE_FEATURE_REMOTE_COMMANDS
/**
 * Helper struct to map our internal status enum to the proto enum and use it in parameterized tests
 */
struct InternalStatusToProto
{
    CommandStatus internalStatus;
    Schemas::Commands::Status protoStatus;
};

inline std::string
statusToString( const testing::TestParamInfo<InternalStatusToProto> &info )
{
    return commandStatusToString( info.param.internalStatus );
}

class DataSenderManagerTestWithAllCommandStatuses : public DataSenderManagerTest,
                                                    public testing::WithParamInterface<InternalStatusToProto>
{
};

INSTANTIATE_TEST_SUITE_P(
    AllCommandStatuses,
    DataSenderManagerTestWithAllCommandStatuses,
    ::testing::Values(
        InternalStatusToProto{ CommandStatus::SUCCEEDED, Schemas::Commands::COMMAND_STATUS_SUCCEEDED },
        InternalStatusToProto{ CommandStatus::EXECUTION_TIMEOUT, Schemas::Commands::COMMAND_STATUS_EXECUTION_TIMEOUT },
        InternalStatusToProto{ CommandStatus::EXECUTION_FAILED, Schemas::Commands::COMMAND_STATUS_EXECUTION_FAILED },
        InternalStatusToProto{ CommandStatus::IN_PROGRESS, Schemas::Commands::COMMAND_STATUS_IN_PROGRESS } ),
    statusToString );

TEST_P( DataSenderManagerTestWithAllCommandStatuses, ProcessSingleCommandResponse )
{
    auto status = GetParam();
    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, Gt( 0 ), _ ) )
        .Times( 1 );

    mDataSenderManager->processData(
        std::make_shared<CommandResponse>( "command123", status.internalStatus, 0x1234, "status456" ) );

    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 1 );
    auto sentBufferData = mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) );

    Schemas::Commands::CommandResponse commandResponse;
    ASSERT_TRUE( commandResponse.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( commandResponse.command_id(), "command123" );
    ASSERT_EQ( commandResponse.status(), status.protoStatus );
    ASSERT_EQ( commandResponse.reason_code(), 0x1234 );
    ASSERT_EQ( commandResponse.reason_description(), "status456" );
}

TEST_P( DataSenderManagerTestWithAllCommandStatuses, PersistencyForCommandResponse )
{
    auto status = GetParam();
    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::NoConnection ) );

    mDataSenderManager->processData(
        std::make_shared<CommandResponse>( "command123", status.internalStatus, 0x1234, "status456" ) );
    mCommandResponseSender->clearSentBufferData();

    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );
    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 1 );
    auto sentBufferData = mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) );

    Schemas::Commands::CommandResponse commandResponse;
    ASSERT_TRUE( commandResponse.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( commandResponse.command_id(), "command123" );
    ASSERT_EQ( commandResponse.status(), status.protoStatus );
    ASSERT_EQ( commandResponse.reason_code(), 0x1234 );
    ASSERT_EQ( commandResponse.reason_description(), "status456" );
}

TEST_F( DataSenderManagerTest, PersistencyForCommandResponsePersistAgainOnFailure )
{
    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::NoConnection ) );

    mDataSenderManager->processData(
        std::make_shared<CommandResponse>( "command123", CommandStatus::EXECUTION_FAILED, 0x1234, "status456" ) );

    mCommandResponseSender->clearSentBufferData();
    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 0 );
    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );

    mDataSenderManager->checkAndSendRetrievedData();

    mCommandResponseSender->clearSentBufferData();
    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 0 );
    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 1 );
    auto sentBufferData = mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) );

    Schemas::Commands::CommandResponse commandResponse;
    ASSERT_TRUE( commandResponse.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( commandResponse.command_id(), "command123" );
    ASSERT_EQ( commandResponse.status(), Schemas::Commands::COMMAND_STATUS_EXECUTION_FAILED );
    ASSERT_EQ( commandResponse.reason_code(), 0x1234 );
    ASSERT_EQ( commandResponse.reason_description(), "status456" );
}

TEST_F( DataSenderManagerTest, InvalidSenderWhenProcessingCommand )
{
    std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders;
    mDataSenderManager = std::make_unique<DataSenderManager>( std::move( dataSenders ), mMqttSender, mPayloadManager );

    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, _, _ ) )
        .Times( 0 );

    mDataSenderManager->processData(
        std::make_shared<CommandResponse>( "command123", CommandStatus::SUCCEEDED, 0x1234, "status456" ) );

    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 0 );
}
#endif

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
class DataSenderManagerTestWithAllSignalTypes : public DataSenderManagerTest,
                                                public testing::WithParamInterface<SignalType>
{
};

INSTANTIATE_TEST_SUITE_P( AllSignalTypes,
                          DataSenderManagerTestWithAllSignalTypes,
                          allSignalTypes,
                          signalTypeParamInfoToString );

void
assertLastKnownStateSignalValue( const Schemas::LastKnownState::CapturedSignal &capturedSignal,
                                 double expectedSignalValue,
                                 SignalType expectedSignalType )
{
    switch ( expectedSignalType )
    {
    case SignalType::UINT8:
        ASSERT_EQ( capturedSignal.uint8_value(), static_cast<uint8_t>( expectedSignalValue ) );
        break;
    case SignalType::INT8:
        ASSERT_EQ( capturedSignal.int8_value(), static_cast<int8_t>( expectedSignalValue ) );
        break;
    case SignalType::UINT16:
        ASSERT_EQ( capturedSignal.uint16_value(), static_cast<uint16_t>( expectedSignalValue ) );
        break;
    case SignalType::INT16:
        ASSERT_EQ( capturedSignal.int16_value(), static_cast<int16_t>( expectedSignalValue ) );
        break;
    case SignalType::UINT32:
        ASSERT_EQ( capturedSignal.uint32_value(), static_cast<uint32_t>( expectedSignalValue ) );
        break;
    case SignalType::INT32:
        ASSERT_EQ( capturedSignal.int32_value(), static_cast<int32_t>( expectedSignalValue ) );
        break;
    case SignalType::UINT64:
        ASSERT_EQ( capturedSignal.uint64_value(), static_cast<uint64_t>( expectedSignalValue ) );
        break;
    case SignalType::INT64:
        ASSERT_EQ( capturedSignal.int64_value(), static_cast<int64_t>( expectedSignalValue ) );
        break;
    case SignalType::FLOAT:
        ASSERT_EQ( capturedSignal.float_value(), static_cast<float>( expectedSignalValue ) );
        break;
    case SignalType::DOUBLE:
        ASSERT_EQ( capturedSignal.double_value(), expectedSignalValue );
        break;
    case SignalType::BOOLEAN:
        ASSERT_EQ( capturedSignal.boolean_value(), static_cast<bool>( expectedSignalValue ) );
        break;
    default:
        FAIL() << "Unsupported signal type";
    }
}

bool
parseLastKnownStateData( const std::string &data, Schemas::LastKnownState::LastKnownStateData *lastKnownStateData )
{
    std::string uncompressedData;
    snappy::Uncompress( data.c_str(), data.size(), &uncompressedData );
    return lastKnownStateData->ParseFromString( uncompressedData );
}

TEST_P( DataSenderManagerTestWithAllSignalTypes, ProcessSingleLastKnownStateSignal )
{
    auto signalType = GetParam();
    EXPECT_CALL( *mLastKnownStateMqttSender, mockedSendBuffer( mLastKnownStateDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    auto collectedData = std::make_shared<LastKnownStateCollectedData>();
    collectedData->triggerTime = 1000;
    collectedData->stateTemplateCollectedSignals.emplace_back(
        StateTemplateCollectedSignals{ "lks1", { { CollectedSignal( 1234, 789654, 40, signalType ) } } } );
    mDataSenderManager->processData( collectedData );

    ASSERT_EQ( mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic ).size(), 1 );
    auto sentBufferData = mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic );

    Schemas::LastKnownState::LastKnownStateData lastKnownStateProto;
    ASSERT_TRUE( parseLastKnownStateData( sentBufferData[0].data, &lastKnownStateProto ) );

    ASSERT_EQ( lastKnownStateProto.collection_event_time_ms_epoch(), 1000 );
    ASSERT_EQ( lastKnownStateProto.captured_state_template_signals_size(), 1 );
    auto &capturedStateTemplateSignals = lastKnownStateProto.captured_state_template_signals()[0];
    ASSERT_EQ( capturedStateTemplateSignals.state_template_sync_id(), "lks1" );
    ASSERT_EQ( capturedStateTemplateSignals.captured_signals_size(), 1 );

    ASSERT_NO_FATAL_FAILURE(
        assertLastKnownStateSignalValue( capturedStateTemplateSignals.captured_signals()[0], 40, signalType ) );
    ASSERT_EQ( capturedStateTemplateSignals.captured_signals()[0].signal_id(), 1234 );
}

TEST_F( DataSenderManagerTest, ProcessMultipleLastKnownStateSignals )
{
    EXPECT_CALL( *mLastKnownStateMqttSender, mockedSendBuffer( mLastKnownStateDataTopic, _, Gt( 0 ), _ ) ).Times( 1 );

    auto collectedData = std::make_shared<LastKnownStateCollectedData>();
    collectedData->triggerTime = 1000;
    collectedData->stateTemplateCollectedSignals.emplace_back( StateTemplateCollectedSignals{
        "lks1",
        { { CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ) },
          { CollectedSignal( 5678, 789700, 97, SignalType::UINT16 ) } },
    } );
    mDataSenderManager->processData( collectedData );

    ASSERT_EQ( mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic ).size(), 1 );
    auto sentBufferData = mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic );

    Schemas::LastKnownState::LastKnownStateData lastKnownStateProto;
    ASSERT_TRUE( parseLastKnownStateData( sentBufferData[0].data, &lastKnownStateProto ) );

    ASSERT_EQ( lastKnownStateProto.collection_event_time_ms_epoch(), 1000 );
    ASSERT_EQ( lastKnownStateProto.captured_state_template_signals_size(), 1 );
    auto &capturedStateTemplateSignals = lastKnownStateProto.captured_state_template_signals()[0];
    ASSERT_EQ( capturedStateTemplateSignals.state_template_sync_id(), "lks1" );

    ASSERT_EQ( capturedStateTemplateSignals.captured_signals_size(), 2 );

    ASSERT_EQ( capturedStateTemplateSignals.captured_signals()[0].double_value(), 40.5 );
    ASSERT_EQ( capturedStateTemplateSignals.captured_signals()[0].signal_id(), 1234 );

    ASSERT_EQ( capturedStateTemplateSignals.captured_signals()[1].uint16_value(), 97 );
    ASSERT_EQ( capturedStateTemplateSignals.captured_signals()[1].signal_id(), 5678 );
}

TEST_F( DataSenderManagerTest, ProcessMultipleLastKnownStateSignalsBeyondTransmitThreshold )
{
    auto collectedData = std::make_shared<LastKnownStateCollectedData>();
    collectedData->triggerTime = 1000;
    auto stateTemplate1 = StateTemplateCollectedSignals{
        "lks1",
        { CollectedSignal( 1234, 788001, 41, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788002, 42, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788003, 43, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788004, 44, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788005, 45, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788006, 46, SignalType::DOUBLE ) },
    };
    collectedData->stateTemplateCollectedSignals.emplace_back( stateTemplate1 );
    auto stateTemplate2 = StateTemplateCollectedSignals{
        "lks2",
        { CollectedSignal( 1234, 788007, 47, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788008, 48, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788009, 49, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788010, 50, SignalType::DOUBLE ),
          CollectedSignal( 1234, 788011, 51, SignalType::DOUBLE ) },
    };
    collectedData->stateTemplateCollectedSignals.emplace_back( stateTemplate2 );

    EXPECT_CALL( *mLastKnownStateMqttSender, mockedSendBuffer( mLastKnownStateDataTopic, _, Gt( 0 ), _ ) ).Times( 3 );
    mDataSenderManager->processData( collectedData );

    // Since mMaxMessagesPerPayload is 5, the 11 signals should be split in 3 messages
    ASSERT_EQ( mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic ).size(), 3 );
    auto sentBufferData = mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic );

    Schemas::LastKnownState::LastKnownStateData lastKnownStateProto;
    ASSERT_TRUE( parseLastKnownStateData( sentBufferData[0].data, &lastKnownStateProto ) );

    ASSERT_EQ( lastKnownStateProto.collection_event_time_ms_epoch(), 1000 );
    ASSERT_EQ( lastKnownStateProto.captured_state_template_signals_size(), 1 );
    auto &capturedStateTemplateSignals = lastKnownStateProto.captured_state_template_signals()[0];
    ASSERT_EQ( capturedStateTemplateSignals.state_template_sync_id(), "lks1" );

    auto &capturedSignals = capturedStateTemplateSignals.captured_signals();

    ASSERT_EQ( getSignalValues( capturedSignals ), ( std::vector<double>{ 41, 42, 43, 44, 45 } ) );

    // Now just ensure that when number of signals is multiple of mMaxMessagesPerPayload, we don't
    // send an empty message.
    mLastKnownStateMqttSender->clearSentBufferData();
    collectedData = std::make_shared<LastKnownStateCollectedData>();
    collectedData->triggerTime = 1000;
    stateTemplate2.signals.pop_back();
    // Sanity check
    ASSERT_EQ( stateTemplate1.signals.size() + stateTemplate2.signals.size(), 10 );
    collectedData->stateTemplateCollectedSignals.emplace_back( stateTemplate1 );
    collectedData->stateTemplateCollectedSignals.emplace_back( stateTemplate2 );
    EXPECT_CALL( *mLastKnownStateMqttSender, mockedSendBuffer( mLastKnownStateDataTopic, _, Gt( 0 ), _ ) ).Times( 2 );

    mDataSenderManager->processData( collectedData );

    ASSERT_EQ( mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic ).size(), 2 );
}

TEST_P( DataSenderManagerTestWithAllSignalTypes, PersistencyForLastKnownState )
{
    auto signalType = GetParam();
    EXPECT_CALL( *mLastKnownStateMqttSender, mockedSendBuffer( mLastKnownStateDataTopic, _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<3>( ConnectivityError::NoConnection ) );

    auto collectedData = std::make_shared<LastKnownStateCollectedData>();
    collectedData->triggerTime = 1000;
    collectedData->stateTemplateCollectedSignals.emplace_back(
        StateTemplateCollectedSignals{ "lks1", { { CollectedSignal( 1234, 789654, 40, signalType ) } } } );
    mDataSenderManager->processData( collectedData );

    mLastKnownStateMqttSender->clearSentBufferData();

    // We don't want to persist LKS data, since old data is not very useful. So nothing should be sent.
    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mLastKnownStateMqttSender->getSentBufferDataByTopic( mLastKnownStateDataTopic ).size(), 0 );
}

TEST_F( DataSenderManagerTest, InvalidSenderWhenProcessingLastKnownState )
{
    std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders;
    mDataSenderManager = std::make_unique<DataSenderManager>( std::move( dataSenders ), mMqttSender, mPayloadManager );

    EXPECT_CALL( *mCommandResponseSender, mockedSendBuffer( getCommandResponseTopic( "command123" ), _, _, _ ) )
        .Times( 0 );

    mDataSenderManager->processData( std::make_shared<LastKnownStateCollectedData>() );

    ASSERT_EQ( mCommandResponseSender->getSentBufferDataByTopic( getCommandResponseTopic( "command123" ) ).size(), 0 );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
