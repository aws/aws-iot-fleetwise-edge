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
        ON_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) )
            .WillByDefault( InvokeArgument<2>( ConnectivityError::Success ) );

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

protected:
    static constexpr unsigned MAXIMUM_PAYLOAD_SIZE = 400;
    static constexpr unsigned CAN_DATA_SIZE = 8;
    PayloadAdaptionConfig mPayloadAdaptionConfigUncompressed{ 80, 70, 90, 10 };
    PayloadAdaptionConfig mPayloadAdaptionConfigCompressed{ 80, 70, 90, 10 };
    unsigned mCanChannelID{ 0 };
    std::shared_ptr<TriggeredCollectionSchemeData> mTriggeredCollectionSchemeData;
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
};

TEST_F( DataSenderManagerTest, senderDataTypeToString )
{
    EXPECT_EQ( senderDataTypeToString( SenderDataType::TELEMETRY ), "Telemetry" );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    EXPECT_EQ( senderDataTypeToString( SenderDataType::VISION_SYSTEM ), "VisionSystem" );
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
    EXPECT_FALSE( stringToSenderDataType( "Invalid", output ) );
}

TEST_F( DataSenderManagerTest, ProcessEmptyData )
{
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );

    // It should just not crash
    processCollectedData( nullptr );
}

TEST_F( DataSenderManagerTest, ProcessSingleSignal )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( InvokeArgument<2>( ConnectivityError::Success ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 2 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 2 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 2 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *mIonWriter, setupVehicleData( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, mockedAppend( _ ) ).Times( 1 );
    EXPECT_CALL( *mIonWriter, getStreambufBuilder() ).Times( 0 );

    processCollectedData( mTriggeredVisionSystemData );
}

TEST_F( DataSenderManagerTest, ProcessVisionSystemDataWithoutRawData )
{
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );
    EXPECT_CALL( *mS3Sender, sendStream( _, _, _, _ ) ).Times( 0 );

    mVisionSystemDataSender->onChangeCollectionSchemeList( mActiveCollectionSchemes );
    processCollectedData( mTriggeredCollectionSchemeData );

    std::shared_ptr<const DataToSend> senderData;
    ASSERT_FALSE( mUploadedS3Objects->pop( senderData ) );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
}
#endif

TEST_F( DataSenderManagerTest, ProcessSingleSignalWithCompression )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );
    mTriggeredCollectionSchemeData->metadata.compress = true;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );

    mDataSenderManager->checkAndSendRetrievedData();
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetryDisabled )
{
    mTriggeredCollectionSchemeData->metadata.persist = false;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::TransmissionError ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).Times( 1 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    ASSERT_EQ( sentBufferData[0].data, payload );

    // Ensure that there is no more data persisted
    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetrySingleFile )
{
    mTriggeredCollectionSchemeData->metadata.persist = true;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::TransmissionError ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyKeepFilesWhenRestarting )
{
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    mTriggeredCollectionSchemeData->metadata.persist = true;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::TransmissionError ) );

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
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
}

TEST_F( DataSenderManagerTest, PersistencyForTelemetryPersistAgainOnFailure )
{
    mTriggeredCollectionSchemeData->metadata.persist = true;
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::NoConnection ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::TransmissionError ) );

    mDataSenderManager->checkAndSendRetrievedData();

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );

    // Now the next attempt succeeds
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillOnce( InvokeArgument<2>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( InvokeArgument<2>( ConnectivityError::NoConnection ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    mMqttSender->clearSentBufferData();
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( InvokeArgument<2>( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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
    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );

    mDataSenderManager->checkAndSendRetrievedData();

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 0 );
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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( AtLeast( 12 ) )
        .WillRepeatedly( InvokeArgument<2>( ConnectivityError::Success ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_GE( mMqttSender->getSentBufferData().size(), 12 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( AtLeast( 9 ) )
        .WillRepeatedly( InvokeArgument<2>( ConnectivityError::Success ) );

    processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_GE( mMqttSender->getSentBufferData().size(), 9 );
    auto sentBufferData = mMqttSender->getSentBufferData();

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

} // namespace IoTFleetWise
} // namespace Aws
