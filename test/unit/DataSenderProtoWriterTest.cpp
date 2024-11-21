// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderProtoWriter.h"
#include "CANDataTypes.h"
#include "CANInterfaceIDTranslator.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "OBDDataTypes.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "TimeTypes.h"
#include "vehicle_data.pb.h"
#include <array>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <cstdlib>
#include <google/protobuf/message.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class DataSenderProtoWriterTest : public ::testing::Test
{
};

TEST_F( DataSenderProtoWriterTest, CollectStringSignalNoRawBufferManager )
{
    CANInterfaceIDTranslator canIDTranslator;
    CollectedSignal stringSignal;
    stringSignal.signalID = 101;
    stringSignal.value.type = SignalType::STRING;
    stringSignal.receiveTime = 5678;

    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    uint32_t collectionEventID = std::rand();
    std::string serializedData;

    triggeredCollectionSchemeDataPtr->metadata.persist = false;
    triggeredCollectionSchemeDataPtr->metadata.compress = false;
    triggeredCollectionSchemeDataPtr->metadata.priority = 0;
    triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metadata.decoderID = "456";
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    DataSenderProtoWriter mDataSenderProtoWriter( canIDTranslator, nullptr );
    mDataSenderProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );

    stringSignal.value.value.uint32Val = static_cast<uint32_t>( 123 );
    mDataSenderProtoWriter.append( stringSignal );

    ASSERT_TRUE( mDataSenderProtoWriter.serializeVehicleData( &serializedData ) );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    vehicleData.ParseFromString( serializedData );
    ASSERT_EQ( vehicleData.captured_signals_size(), 0 );
}

TEST_F( DataSenderProtoWriterTest, CollectStringSignal )
{
    CANInterfaceIDTranslator canIDTranslator;
    CollectedSignal stringSignal;
    TimePoint timestamp = { 160000000, 100 };
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    RawData::SignalBufferOverrides signalOverrides1;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    uint32_t collectionEventID = std::rand();
    std::string stringData = "1BDD00";
    std::string serializedData;

    stringSignal.signalID = 101;
    stringSignal.value.type = SignalType::STRING;
    stringSignal.receiveTime = 5678;

    signalUpdateConfig1.typeId = stringSignal.signalID;
    signalUpdateConfig1.interfaceId = "interface1";
    signalUpdateConfig1.messageId = "VEHICLE.DTC_INFO";
    signalOverrides1.interfaceId = signalUpdateConfig1.interfaceId;
    signalOverrides1.messageId = signalUpdateConfig1.messageId;
    signalOverrides1.maxNumOfSamples = 20;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.reservedBytes = 5_MiB;
    signalOverrides1.maxBytes = 100_MiB;

    triggeredCollectionSchemeDataPtr->metadata.persist = false;
    triggeredCollectionSchemeDataPtr->metadata.compress = false;
    triggeredCollectionSchemeDataPtr->metadata.priority = 0;
    triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metadata.decoderID = "456";
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    overridesPerSignal = { signalOverrides1 };

    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::make_optional( (size_t)20 ), boost::none, boost::none, overridesPerSignal );

    updatedSignals = { { signalUpdateConfig1.typeId, signalUpdateConfig1 } };

    std::shared_ptr<RawData::BufferManager> rawDataBufferManager =
        std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
    rawDataBufferManager->updateConfig( updatedSignals );

    DataSenderProtoWriter mDataSenderProtoWriter( canIDTranslator, rawDataBufferManager );
    mDataSenderProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );

    auto handle = rawDataBufferManager->push(
        (uint8_t *)stringData.c_str(), stringData.length(), timestamp.systemTimeMs, stringSignal.signalID );

    rawDataBufferManager->increaseHandleUsageHint(
        101, handle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD );

    stringSignal.value.value.uint32Val = static_cast<uint32_t>( handle );
    mDataSenderProtoWriter.append( stringSignal );
    // Invalid buffer handle
    mDataSenderProtoWriter.append( CollectedSignal( 123,                            // signalId
                                                    timestamp.systemTimeMs,         // receiveTime,
                                                    RawData::INVALID_BUFFER_HANDLE, // value
                                                    SignalType::STRING ) );

    ASSERT_TRUE( mDataSenderProtoWriter.serializeVehicleData( &serializedData ) );

    Schemas::VehicleDataMsg::VehicleData vehicleData;
    vehicleData.ParseFromString( serializedData );
    ASSERT_EQ( vehicleData.captured_signals_size(), 1 );
    EXPECT_EQ( vehicleData.captured_signals( 0 ).signal_id(), stringSignal.signalID );
    EXPECT_EQ( vehicleData.captured_signals( 0 ).string_value(), stringData.c_str() );
}

// Test the edge to cloud payload fields in the proto
TEST_F( DataSenderProtoWriterTest, TestVehicleData )
{
    CANInterfaceIDTranslator canIDTranslator;
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    DataSenderProtoWriter protoWriter( canIDTranslator, mRawDataBufferManager );
    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeDataPtr->metadata.persist = false;
    triggeredCollectionSchemeDataPtr->metadata.compress = false;
    triggeredCollectionSchemeDataPtr->metadata.priority = 0;
    triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metadata.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );
    protoWriter.append( CollectedSignal( 121,                         // signalId
                                         testTriggerTime + 2000,      // receiveTime,
                                         static_cast<uint8_t>( 123 ), // value
                                         SignalType::UINT8 ) );
    protoWriter.append( CollectedSignal( 122,                          // signalId
                                         testTriggerTime + 2000,       // receiveTime,
                                         static_cast<uint16_t>( 456 ), // value
                                         SignalType::UINT16 ) );
    protoWriter.append( CollectedSignal( 123,                          // signalId
                                         testTriggerTime + 2000,       // receiveTime,
                                         static_cast<uint32_t>( 789 ), // value
                                         SignalType::UINT32 ) );
    protoWriter.append( CollectedSignal( 124,                         // signalId
                                         testTriggerTime + 2000,      // receiveTime,
                                         static_cast<uint64_t>( 55 ), // value
                                         SignalType::UINT64 ) );
    protoWriter.append( CollectedSignal( 125,                         // signalId
                                         testTriggerTime + 2000,      // receiveTime,
                                         static_cast<int8_t>( -123 ), // value
                                         SignalType::INT8 ) );
    protoWriter.append( CollectedSignal( 126,                          // signalId
                                         testTriggerTime + 2000,       // receiveTime,
                                         static_cast<int16_t>( -456 ), // value
                                         SignalType::INT16 ) );
    protoWriter.append( CollectedSignal( 127,                          // signalId
                                         testTriggerTime + 2000,       // receiveTime,
                                         static_cast<int32_t>( -789 ), // value
                                         SignalType::INT32 ) );
    protoWriter.append( CollectedSignal( 128,                         // signalId
                                         testTriggerTime + 2000,      // receiveTime,
                                         static_cast<int64_t>( -55 ), // value
                                         SignalType::INT64 ) );
    protoWriter.append( CollectedSignal( 129,                         // signalId
                                         testTriggerTime + 2000,      // receiveTime,
                                         static_cast<float>( 11.22 ), // value
                                         SignalType::FLOAT ) );
    protoWriter.append( CollectedSignal( 130,                    // signalId
                                         testTriggerTime + 2000, // receiveTime,
                                         77.88,                  // value
                                         SignalType::DOUBLE ) );
    protoWriter.append( CollectedSignal( 130,                    // signalId
                                         testTriggerTime + 2000, // receiveTime,
                                         true,                   // value
                                         SignalType::BOOLEAN ) );
    // Not supported, so won't increase estimated size
    protoWriter.append( CollectedSignal( 131,                    // signalId
                                         testTriggerTime + 2000, // receiveTime,
                                         0,                      // value
                                         SignalType::UNKNOWN ) );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    // Not supported, so won't increase estimated size
    protoWriter.append( CollectedSignal( 132,                    // signalId
                                         testTriggerTime + 2000, // receiveTime,
                                         0,                      // value
                                         SignalType::COMPLEX_SIGNAL ) );
#endif
    EXPECT_EQ( protoWriter.getVehicleDataEstimatedSize(), 242 );

    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CollectedCanRawFrame canRawFrameMsg(
        12 /*frameId*/, 1 /*nodeId*/, testTriggerTime + 1000 /*receiveTime*/, data, 8 /*sizeof data*/ );
    protoWriter.append( canRawFrameMsg );
    EXPECT_EQ( protoWriter.getVehicleDataEstimatedSize(), 266 );
    std::string out;
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );

    Schemas::VehicleDataMsg::VehicleData vehicleDataTest{};

    ASSERT_TRUE( vehicleDataTest.ParseFromString( out ) );

    /* Read and compare to written fields */
    ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );
}

// Test the DTC fields in the proto for the edge to cloud payload
TEST_F( DataSenderProtoWriterTest, TestDTCData )
{
    CANInterfaceIDTranslator canIDTranslator;
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    DataSenderProtoWriter protoWriter( canIDTranslator, mRawDataBufferManager );

    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeDataPtr->metadata.persist = false;
    triggeredCollectionSchemeDataPtr->metadata.compress = false;
    triggeredCollectionSchemeDataPtr->metadata.priority = 0;
    triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metadata.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );

    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = testTriggerTime + 2000;
    dtcInfo.mDTCCodes = { "U0123", "P0456" };

    protoWriter.setupDTCInfo( dtcInfo );

    protoWriter.append( dtcInfo.mDTCCodes[0] );
    EXPECT_EQ( protoWriter.getVehicleDataEstimatedSize(), 37 );

    protoWriter.append( dtcInfo.mDTCCodes[1] );
    EXPECT_EQ( protoWriter.getVehicleDataEstimatedSize(), 44 );

    std::string out;
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );

    Schemas::VehicleDataMsg::VehicleData vehicleDataTest{};

    ASSERT_TRUE( vehicleDataTest.ParseFromString( out ) );

    /* Read and compare to written fields */
    ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );

    auto dtcData = vehicleDataTest.mutable_dtc_data();
    ASSERT_EQ( dtcData->relative_time_ms(), dtcInfo.receiveTime - testTriggerTime );
    ASSERT_EQ( "U0123", dtcData->active_dtc_codes( 0 ) );
    ASSERT_EQ( "P0456", dtcData->active_dtc_codes( 1 ) );
}

TEST_F( DataSenderProtoWriterTest, splitAndMerge )
{
    CANInterfaceIDTranslator canIDTranslator;
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    DataSenderProtoWriter protoWriter( canIDTranslator, mRawDataBufferManager );
    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeDataPtr->metadata.persist = false;
    triggeredCollectionSchemeDataPtr->metadata.compress = false;
    triggeredCollectionSchemeDataPtr->metadata.priority = 0;
    triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metadata.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );
    protoWriter.append( CollectedSignal( 121,                         // signalId
                                         testTriggerTime + 2000,      // receiveTime,
                                         static_cast<uint8_t>( 123 ), // value
                                         SignalType::UINT8 ) );
    protoWriter.append( CollectedSignal( 122,                          // signalId
                                         testTriggerTime + 2000,       // receiveTime,
                                         static_cast<uint16_t>( 456 ), // value
                                         SignalType::UINT16 ) );
    protoWriter.append( CollectedSignal( 123,                          // signalId
                                         testTriggerTime + 2000,       // receiveTime,
                                         static_cast<uint32_t>( 789 ), // value
                                         SignalType::UINT32 ) );

    DTCInfo dtcInfo;
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = testTriggerTime + 2000;
    dtcInfo.mDTCCodes = { "U0123", "P0456" };

    protoWriter.setupDTCInfo( dtcInfo );

    protoWriter.append( dtcInfo.mDTCCodes[0] );
    protoWriter.append( dtcInfo.mDTCCodes[1] );

    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> canData = { 1, 2, 3, 4, 5, 6, 7, 8 };
    protoWriter.append(
        CollectedCanRawFrame{ 12 /*frameId*/, 1 /*nodeId*/, testTriggerTime + 1000, canData, 8 /*size*/ } );
    protoWriter.append(
        CollectedCanRawFrame{ 13 /*frameId*/, 1 /*nodeId*/, testTriggerTime + 2000, canData, 8 /*size*/ } );
    protoWriter.append(
        CollectedCanRawFrame{ 14 /*frameId*/, 1 /*nodeId*/, testTriggerTime + 3000, canData, 8 /*size*/ } );
    protoWriter.append(
        CollectedCanRawFrame{ 15 /*frameId*/, 1 /*nodeId*/, testTriggerTime + 3000, canData, 8 /*size*/ } );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    protoWriter.append( UploadedS3Object{ "abc1", UploadedS3ObjectDataFormat::Cdr } );
    protoWriter.append( UploadedS3Object{ "abc2", UploadedS3ObjectDataFormat::Cdr } );
    protoWriter.append( UploadedS3Object{ "abc3", UploadedS3ObjectDataFormat::Cdr } );
#endif

    Schemas::VehicleDataMsg::VehicleData data;
    protoWriter.splitVehicleData( data );

    std::string out;
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );

    Schemas::VehicleDataMsg::VehicleData vehicleDataTest{};

    ASSERT_TRUE( vehicleDataTest.ParseFromString( out ) );

    /* Read and compare to written fields */
    ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );

    ASSERT_EQ( vehicleDataTest.captured_signals().size(), 1 );
    EXPECT_EQ( vehicleDataTest.captured_signals()[0].signal_id(), 121 );
    ASSERT_EQ( vehicleDataTest.dtc_data().active_dtc_codes_size(), 1 );
    EXPECT_EQ( vehicleDataTest.dtc_data().active_dtc_codes()[0], "U0123" );
    ASSERT_EQ( vehicleDataTest.can_frames().size(), 2 );
    EXPECT_EQ( vehicleDataTest.can_frames()[0].message_id(), 12 );
    EXPECT_EQ( vehicleDataTest.can_frames()[1].message_id(), 13 );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ASSERT_EQ( vehicleDataTest.s3_objects().size(), 1 );
    EXPECT_EQ( vehicleDataTest.s3_objects()[0].key(), "abc1" );
#endif

    protoWriter.mergeVehicleData( data );
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );
    ASSERT_TRUE( vehicleDataTest.ParseFromString( out ) );

    /* Read and compare to written fields */
    ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );

    ASSERT_EQ( vehicleDataTest.captured_signals().size(), 2 );
    EXPECT_EQ( vehicleDataTest.captured_signals()[0].signal_id(), 122 );
    EXPECT_EQ( vehicleDataTest.captured_signals()[1].signal_id(), 123 );
    ASSERT_EQ( vehicleDataTest.dtc_data().active_dtc_codes_size(), 1 );
    EXPECT_EQ( vehicleDataTest.dtc_data().active_dtc_codes()[0], "P0456" );
    ASSERT_EQ( vehicleDataTest.can_frames().size(), 2 );
    EXPECT_EQ( vehicleDataTest.can_frames()[0].message_id(), 14 );
    EXPECT_EQ( vehicleDataTest.can_frames()[1].message_id(), 15 );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ASSERT_EQ( vehicleDataTest.s3_objects().size(), 2 );
    EXPECT_EQ( vehicleDataTest.s3_objects()[0].key(), "abc2" );
    EXPECT_EQ( vehicleDataTest.s3_objects()[1].key(), "abc3" );
#endif
}

} // namespace IoTFleetWise
} // namespace Aws
