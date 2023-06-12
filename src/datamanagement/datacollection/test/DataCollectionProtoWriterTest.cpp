// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataCollectionProtoWriter.h"
#include "ClockHandler.h"
#include "Testing.h"
#include <gtest/gtest.h>
#include <math.h>

using namespace Aws::IoTFleetWise::TestingSupport;
using namespace Aws::IoTFleetWise::DataManagement;
using namespace Aws::IoTFleetWise::Platform::Linux;

class DataCollectionProtoWriterTest : public ::testing::Test
{
public:
    std::string
    convertProtoToHex( const std::string &proto )
    {
        std::string hex;
        for ( size_t i = 0U; i < proto.size(); i++ )
        {
            char hexBuf[3];
            sprintf( hexBuf, "%02X", (unsigned char)proto[i] );
            hex += hexBuf;
        }
        return hex;
    }
};

// Test the edge to cloud payload fields in the proto
TEST_F( DataCollectionProtoWriterTest, TestVehicleData )
{
    CANInterfaceIDTranslator canIDTranslator;
    DataCollectionProtoWriter protoWriter( canIDTranslator );
    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeDataPtr->metaData.persist = false;
    triggeredCollectionSchemeDataPtr->metaData.compress = false;
    triggeredCollectionSchemeDataPtr->metaData.priority = 0;
    triggeredCollectionSchemeDataPtr->metaData.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metaData.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );
    CollectedSignal collectedSignalMsg(
        120 /*signalId*/, testTriggerTime + 2000 /*receiveTime*/, 77.88 /*value*/, SignalType::DOUBLE );
    protoWriter.append( collectedSignalMsg );
    EXPECT_EQ( protoWriter.getVehicleDataMsgCount(), 1 );

    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CollectedCanRawFrame canRawFrameMsg(
        12 /*frameId*/, 1 /*nodeId*/, testTriggerTime + 1000 /*receiveTime*/, data, 8 /*sizeof data*/ );
    protoWriter.append( canRawFrameMsg );
    EXPECT_EQ( protoWriter.getVehicleDataMsgCount(), 2 );
    std::string out;
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );

    VehicleDataMsg::VehicleData vehicleDataTest{};

    const std::string testProto = out;
    if ( !vehicleDataTest.ParseFromString( testProto ) )
    {
        ASSERT_TRUE( false );
    }

    /* Read and compare to written fields */
    ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );
}

// Test the DTC fields in the proto for the edge to cloud payload
TEST_F( DataCollectionProtoWriterTest, TestDTCData )
{
    CANInterfaceIDTranslator canIDTranslator;
    DataCollectionProtoWriter protoWriter( canIDTranslator );

    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeDataPtr->metaData.persist = false;
    triggeredCollectionSchemeDataPtr->metaData.compress = false;
    triggeredCollectionSchemeDataPtr->metaData.priority = 0;
    triggeredCollectionSchemeDataPtr->metaData.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metaData.decoderID = "456";
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
    EXPECT_EQ( protoWriter.getVehicleDataMsgCount(), 1 );

    protoWriter.append( dtcInfo.mDTCCodes[1] );
    EXPECT_EQ( protoWriter.getVehicleDataMsgCount(), 2 );

    std::string out;
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );

    VehicleDataMsg::VehicleData vehicleDataTest{};

    const std::string testProto = out;
    if ( !vehicleDataTest.ParseFromString( testProto ) )
    {
        ASSERT_TRUE( false );
    }

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

// Test the Geohash fields in the proto for the edge to cloud payload
TEST_F( DataCollectionProtoWriterTest, TestGeohash )
{
    CANInterfaceIDTranslator canIDTranslator;
    DataCollectionProtoWriter protoWriter( canIDTranslator );

    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeDataPtr =
        std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeDataPtr->metaData.persist = false;
    triggeredCollectionSchemeDataPtr->metaData.compress = false;
    triggeredCollectionSchemeDataPtr->metaData.priority = 0;
    triggeredCollectionSchemeDataPtr->metaData.collectionSchemeID = "123";
    triggeredCollectionSchemeDataPtr->metaData.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );

    GeohashInfo geohashInfo;
    geohashInfo.mGeohashString = "9q9hwg28j";
    geohashInfo.mPrevReportedGeohashString = "9q9hwg281";

    protoWriter.append( geohashInfo );
    EXPECT_EQ( protoWriter.getVehicleDataMsgCount(), 1 );

    std::string out;
    EXPECT_TRUE( protoWriter.serializeVehicleData( &out ) );

    VehicleDataMsg::VehicleData vehicleDataTest{};

    const std::string testProto = out;
    ASSERT_TRUE( vehicleDataTest.ParseFromString( testProto ) );

    /* Read and compare to written fields */
    ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );

    auto geohash = vehicleDataTest.mutable_geohash();
    ASSERT_EQ( "9q9hwg28j", geohash->geohash_string() );
    ASSERT_EQ( "9q9hwg281", geohash->prev_reported_geohash_string() );
}
