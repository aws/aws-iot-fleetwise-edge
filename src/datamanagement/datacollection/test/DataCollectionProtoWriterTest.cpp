/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "DataCollectionProtoWriter.h"
#include "ClockHandler.h"
#include <gtest/gtest.h>
#include <math.h>

using namespace Aws::IoTFleetWise::DataManagement;
using namespace Aws::IoTFleetWise::Platform::Linux;

class DataCollectionProtoWriterTest : public ::testing::Test
{
public:
    void
    check( double input, double diff, bool print = true )
    {
        uint32_t quotient;
        uint32_t divisor;
        DataCollectionProtoWriter::convertToPeculiarFloat( input, quotient, divisor );
        double output = (double)quotient / (double)divisor;
        if ( print )
        {
            printf( "input=%g, output=%u/%u=%g\n", input, quotient, divisor, output );
        }
        if ( isnan( input ) || isinf( input ) )
        {
            EXPECT_EQ( UINT32_MAX, output );
        }
        else
        {
            EXPECT_NEAR( abs( input ), output, abs( input ) * diff );
        }
    }

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

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatZero )
{
    check( 0.0, 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloat1 )
{
    check( 1.0, 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatMinus1 )
{
    check( -1.0, 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloat1000 )
{
    check( 1000.0, 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatU32Max )
{
    check( UINT32_MAX, 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloat0_5 )
{
    check( 0.5, 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloat0_333 )
{
    check( 1.0 / 3.0, 0.000000001 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloat0_111 )
{
    check( 1.0 / 9.0, 0.000000001 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatMin )
{
    check( 1.0 / (double)( 1U << 31 ), 0.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatEpsilon )
{
    check( DBL_EPSILON, 1.0 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatMoreThan9SigFig )
{
    // This test broke decimalToFraction
    check( 5.000000001, 0.000000001 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatPi )
{
    check( M_PI, 0.000000001 );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatInfinity )
{
    check( INFINITY, INFINITY );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatMinusInfinity )
{
    check( -INFINITY, INFINITY );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatNan )
{
    check( NAN, NAN );
}

TEST_F( DataCollectionProtoWriterTest, TestPeculiarFloatRandom )
{
    srand( (unsigned)time( NULL ) );
    for ( auto i = 0; i < 10000000; i++ )
    {
        double r = ( (double)rand() * 100000.0 ) / (double)RAND_MAX;
        check( r, 0.000000001, false );
    }
}

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
    Timestamp testTriggerTime = testClock->timeSinceEpochMs();
    triggeredCollectionSchemeDataPtr->triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, collectionEventID );
    CollectedSignal collectedSignalMsg( 120 /*signalId*/, testTriggerTime + 2000 /*receiveTime*/, 77.88 /*value*/ );
    protoWriter.append( collectedSignalMsg );
    EXPECT_EQ( protoWriter.getVehicleDataMsgCount(), 1 );

    std::array<uint8_t, 8> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
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
    ASSERT_EQ( "123", vehicleDataTest.campaign_arn() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_arn() );
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
    Timestamp testTriggerTime = testClock->timeSinceEpochMs();
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
    ASSERT_EQ( "123", vehicleDataTest.campaign_arn() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_arn() );
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
    Timestamp testTriggerTime = testClock->timeSinceEpochMs();
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
    ASSERT_EQ( "123", vehicleDataTest.campaign_arn() );
    ASSERT_EQ( "456", vehicleDataTest.decoder_arn() );
    ASSERT_EQ( collectionEventID, vehicleDataTest.collection_event_id() );
    ASSERT_EQ( testTriggerTime, vehicleDataTest.collection_event_time_ms_epoch() );

    auto geohash = vehicleDataTest.mutable_geohash();
    ASSERT_EQ( "9q9hwg28j", geohash->geohash_string() );
    ASSERT_EQ( "9q9hwg281", geohash->prev_reported_geohash_string() );
}