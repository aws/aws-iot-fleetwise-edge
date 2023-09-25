// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManager.h"
#include "CANDataTypes.h"
#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "CollectionInspectionAPITypes.h"
#include "GeohashInfo.h"
#include "IConnectionTypes.h"
#include "ISender.h"
#include "OBDDataTypes.h"
#include "PayloadManagerMock.h"
#include "SenderMock.h"
#include "SignalTypes.h"
#include "vehicle_data.pb.h"
#include <array>
#include <cstdint>
#include <gmock/gmock.h>
#include <google/protobuf/message.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <snappy.h>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::DoAll;
using ::testing::Gt;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

class DataSenderManagerTest : public ::testing::Test
{
public:
    void
    SetUp() override
    {
        mTriggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();
        mTriggeredCollectionSchemeData->metadata.decoderID = "TESTDECODERID";
        mTriggeredCollectionSchemeData->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME";
        mTriggeredCollectionSchemeData->triggerTime = 1000000;
        mTriggeredCollectionSchemeData->eventID = 579;

        mCANIDTranslator.add( "can123" );

        mMqttSender = std::make_shared<StrictMock<Testing::SenderMock>>();
        mPayloadManager = std::make_shared<StrictMock<Testing::PayloadManagerMock>>();
        mDataSenderManager =
            std::make_unique<DataSenderManager>( mMqttSender, mPayloadManager, mCANIDTranslator, mTransmitThreshold );
    }

    void
    TearDown() override
    {
    }

protected:
    unsigned mTransmitThreshold{ 5 }; // max number of messages that can be sent to cloud at one time
    unsigned mCanChannelID{ 0 };
    std::shared_ptr<TriggeredCollectionSchemeData> mTriggeredCollectionSchemeData;
    std::shared_ptr<StrictMock<Testing::SenderMock>> mMqttSender;
    std::shared_ptr<StrictMock<Testing::PayloadManagerMock>> mPayloadManager;
    CANInterfaceIDTranslator mCANIDTranslator;
    std::unique_ptr<DataSenderManager> mDataSenderManager;
};

TEST_F( DataSenderManagerTest, ProcessEmptyData )
{
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, _, _ ) ).Times( 0 );

    // It should just not crash
    mDataSenderManager->processCollectedData( nullptr );
}

TEST_F( DataSenderManagerTest, ProcessSingleSignal )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 3 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

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
                                             CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE ) };
    mTriggeredCollectionSchemeData->signals = signals;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    collectionSchemeParams = sentBufferData[1].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 5 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 4 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );
}

TEST_F( DataSenderManagerTest, ProcessSingleCanFrame )
{
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame1 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) );
    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame1 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 1 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

    ASSERT_EQ( vehicleData.can_frames()[0].message_id(), canFrame1.frameID );
    ASSERT_EQ( vehicleData.can_frames()[0].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[0].byte_values(), std::string( canBuf1.begin(), canBuf1.end() ) );
}

TEST_F( DataSenderManagerTest, ProcessMultipleCanFrames )
{
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame1 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf2 = { 0xBA, 0xAD, 0xAF, 0xFE, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame2 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf2, sizeof( canBuf2 ) );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf3 = { 0xCA, 0xFE, 0xF0, 0x0D, 0x0, 0x0, 0x0, 0x0 };
    auto canFrame3 = CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf3, sizeof( canBuf3 ) );

    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame1 );
    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame2 );
    mTriggeredCollectionSchemeData->canFrames.push_back( canFrame3 );

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 3 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

    ASSERT_EQ( vehicleData.can_frames()[0].message_id(), canFrame1.frameID );
    ASSERT_EQ( vehicleData.can_frames()[0].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[0].byte_values(), std::string( canBuf1.begin(), canBuf1.end() ) );
    ASSERT_EQ( vehicleData.can_frames()[1].message_id(), canFrame2.frameID );
    ASSERT_EQ( vehicleData.can_frames()[1].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[1].byte_values(), std::string( canBuf2.begin(), canBuf2.end() ) );
    ASSERT_EQ( vehicleData.can_frames()[2].message_id(), canFrame3.frameID );
    ASSERT_EQ( vehicleData.can_frames()[2].interface_id(), "can123" );
    ASSERT_EQ( vehicleData.can_frames()[2].byte_values(), std::string( canBuf3.begin(), canBuf3.end() ) );
}

TEST_F( DataSenderManagerTest, ProcessMultipleCanFramesBeyondTransmitThreshold )
{
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canBuf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    std::vector<CollectedCanRawFrame> canFrames = {
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ),
        CollectedCanRawFrame( 0x380, mCanChannelID, 789654, canBuf1, sizeof( canBuf1 ) ) };
    mTriggeredCollectionSchemeData->canFrames = canFrames;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    collectionSchemeParams = sentBufferData[1].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 5 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 4 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );
}

TEST_F( DataSenderManagerTest, ProcessSingleDtcCode )
{
    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = 789654;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    mTriggeredCollectionSchemeData->mDTCInfo = dtcInfo;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

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

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 2 );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes()[0], "P0143" );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes()[1], "C0196" );
}

TEST_F( DataSenderManagerTest, ProcessMultipleDtcCodesBeyondTransmitThreshold )
{
    std::vector<std::string> dtcCodes = {
        "P0143", "C0196", "U0148", "B0148", "C0148", "C0149", "C0150", "C0151", "C0152" };

    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = 789654;
    dtcInfo.mDTCCodes = dtcCodes;
    mTriggeredCollectionSchemeData->mDTCInfo = dtcInfo;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 2 )
        .WillRepeatedly( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 2 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 5 );

    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[1].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_TRUE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );
    ASSERT_EQ( vehicleData.dtc_data().active_dtc_codes_size(), 4 );
}

TEST_F( DataSenderManagerTest, ProcessGeohash )
{
    GeohashInfo geohashInfo;
    geohashInfo.mGeohashString = "9q9hwg28j";
    mTriggeredCollectionSchemeData->mGeohashInfo = geohashInfo;

    // TODO: verify data and collectionSchemeParams (3rd parameter)
    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    ASSERT_TRUE( vehicleData.ParseFromString( sentBufferData[0].data ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_TRUE( vehicleData.has_geohash() );

    ASSERT_EQ( vehicleData.geohash().geohash_string(), "9q9hwg28j" );
}

TEST_F( DataSenderManagerTest, PersistencyNoFiles )
{
    Json::Value files( Json::arrayValue );

    EXPECT_CALL( *mPayloadManager, retrievePayloadMetadata( _ ) )
        .WillOnce( DoAll( SetArgReferee<0>( files ), Return( ErrorCode::SUCCESS ) ) );

    EXPECT_CALL( *mMqttSender, sendFile( _, _, _ ) ).Times( 0 );

    mDataSenderManager->checkAndSendRetrievedData();
}

TEST_F( DataSenderManagerTest, ProcessSingleSignalWithCompression )
{
    auto signal1 = CollectedSignal( 1234, 789654, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );
    mTriggeredCollectionSchemeData->metadata.compress = true;

    EXPECT_CALL( *mMqttSender, mockedSendBuffer( _, Gt( 0 ), _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->processCollectedData( mTriggeredCollectionSchemeData );

    ASSERT_EQ( mMqttSender->getSentBufferData().size(), 1 );
    auto sentBufferData = mMqttSender->getSentBufferData();

    auto collectionSchemeParams = sentBufferData[0].collectionSchemeParams;
    ASSERT_EQ( collectionSchemeParams.eventID, mTriggeredCollectionSchemeData->eventID );
    ASSERT_EQ( collectionSchemeParams.triggerTime, mTriggeredCollectionSchemeData->triggerTime );
    ASSERT_EQ( collectionSchemeParams.compression, mTriggeredCollectionSchemeData->metadata.compress );
    ASSERT_EQ( collectionSchemeParams.persist, mTriggeredCollectionSchemeData->metadata.persist );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    std::string uncompressedData;
    snappy::Uncompress( sentBufferData[0].data.c_str(), sentBufferData[0].data.size(), &uncompressedData );
    ASSERT_TRUE( vehicleData.ParseFromString( uncompressedData ) );

    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    ASSERT_EQ( vehicleData.can_frames_size(), 0 );
    ASSERT_FALSE( vehicleData.has_dtc_data() );
    ASSERT_FALSE( vehicleData.has_geohash() );

    ASSERT_EQ( vehicleData.captured_signals()[0].signal_id(), signal1.signalID );
    ASSERT_EQ( vehicleData.captured_signals()[0].double_value(), signal1.value.value.doubleVal );
}

TEST_F( DataSenderManagerTest, PersistencySingleFile )
{
    Json::Value files( Json::arrayValue );

    files.append( Json::objectValue );
    files[0]["filename"] = "filename1";
    files[0]["compressionRequired"] = false;
    files[0]["payloadSize"] = 1000;

    EXPECT_CALL( *mPayloadManager, retrievePayloadMetadata( _ ) )
        .WillOnce( DoAll( SetArgReferee<0>( files ), Return( ErrorCode::SUCCESS ) ) );

    EXPECT_CALL( *mMqttSender, sendFile( "filename1", 1000, _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();
}

TEST_F( DataSenderManagerTest, PersistencyMultipleFiles )
{
    Json::Value files( Json::arrayValue );

    files.append( Json::objectValue );
    files[0]["filename"] = "filename1";
    files[0]["compressionRequired"] = false;
    files[0]["payloadSize"] = 1000;

    files.append( Json::objectValue );
    files[1]["filename"] = "filename2";
    files[1]["compressionRequired"] = false;
    files[1]["payloadSize"] = 3000;

    EXPECT_CALL( *mPayloadManager, retrievePayloadMetadata( _ ) )
        .WillOnce( DoAll( SetArgReferee<0>( files ), Return( ErrorCode::SUCCESS ) ) );

    EXPECT_CALL( *mMqttSender, sendFile( "filename1", 1000, _ ) )
        .WillOnce( Return( ConnectivityError::WrongInputData ) );
    EXPECT_CALL( *mMqttSender, sendFile( "filename2", 3000, _ ) ).WillOnce( Return( ConnectivityError::Success ) );

    mDataSenderManager->checkAndSendRetrievedData();
}

} // namespace IoTFleetWise
} // namespace Aws
