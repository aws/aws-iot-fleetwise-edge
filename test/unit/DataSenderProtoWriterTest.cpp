// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "Testing.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "vehicle_data.pb.h"
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

    TriggeredCollectionSchemeData triggeredCollectionSchemeData;
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    uint32_t collectionEventID = std::rand();
    std::string serializedData;

    triggeredCollectionSchemeData.metadata.persist = false;
    triggeredCollectionSchemeData.metadata.compress = false;
    triggeredCollectionSchemeData.metadata.priority = 0;
    triggeredCollectionSchemeData.metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeData.metadata.decoderID = "456";
    triggeredCollectionSchemeData.triggerTime = testTriggerTime;

    DataSenderProtoWriter mDataSenderProtoWriter( canIDTranslator, nullptr );
    mDataSenderProtoWriter.setupVehicleData( triggeredCollectionSchemeData, collectionEventID );

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
    RawData::SignalUpdateConfig signalUpdateConfig1;
    RawData::SignalBufferOverrides signalOverrides1;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    TriggeredCollectionSchemeData triggeredCollectionSchemeData;
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

    triggeredCollectionSchemeData.metadata.persist = false;
    triggeredCollectionSchemeData.metadata.compress = false;
    triggeredCollectionSchemeData.metadata.priority = 0;
    triggeredCollectionSchemeData.metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeData.metadata.decoderID = "456";
    triggeredCollectionSchemeData.triggerTime = testTriggerTime;

    overridesPerSignal = { signalOverrides1 };

    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::make_optional( (size_t)20 ), boost::none, boost::none, overridesPerSignal );

    updatedSignals = { { signalUpdateConfig1.typeId, signalUpdateConfig1 } };

    RawData::BufferManager rawDataBufferManager( rawDataBufferManagerConfig.get() );
    rawDataBufferManager.updateConfig( updatedSignals );

    DataSenderProtoWriter mDataSenderProtoWriter( canIDTranslator, &rawDataBufferManager );
    mDataSenderProtoWriter.setupVehicleData( triggeredCollectionSchemeData, collectionEventID );

    auto handle = rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                                             stringData.length(),
                                             timestamp.systemTimeMs,
                                             stringSignal.signalID );

    rawDataBufferManager.increaseHandleUsageHint(
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
    RawData::BufferManager rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    DataSenderProtoWriter protoWriter( canIDTranslator, &rawDataBufferManager );
    TriggeredCollectionSchemeData triggeredCollectionSchemeData;
    triggeredCollectionSchemeData.metadata.persist = false;
    triggeredCollectionSchemeData.metadata.compress = false;
    triggeredCollectionSchemeData.metadata.priority = 0;
    triggeredCollectionSchemeData.metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeData.metadata.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeData.triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeData, collectionEventID );
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
    RawData::BufferManager rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    DataSenderProtoWriter protoWriter( canIDTranslator, &rawDataBufferManager );

    TriggeredCollectionSchemeData triggeredCollectionSchemeData;
    triggeredCollectionSchemeData.metadata.persist = false;
    triggeredCollectionSchemeData.metadata.compress = false;
    triggeredCollectionSchemeData.metadata.priority = 0;
    triggeredCollectionSchemeData.metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeData.metadata.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeData.triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeData, collectionEventID );

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
    RawData::BufferManager rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    DataSenderProtoWriter protoWriter( canIDTranslator, &rawDataBufferManager );
    TriggeredCollectionSchemeData triggeredCollectionSchemeData;
    triggeredCollectionSchemeData.metadata.persist = false;
    triggeredCollectionSchemeData.metadata.compress = false;
    triggeredCollectionSchemeData.metadata.priority = 0;
    triggeredCollectionSchemeData.metadata.collectionSchemeID = "123";
    triggeredCollectionSchemeData.metadata.decoderID = "456";
    // Set the trigger time to current time
    auto testClock = ClockHandler::getClock();
    Timestamp testTriggerTime = testClock->systemTimeSinceEpochMs();
    triggeredCollectionSchemeData.triggerTime = testTriggerTime;

    uint32_t collectionEventID = std::rand();
    protoWriter.setupVehicleData( triggeredCollectionSchemeData, collectionEventID );
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
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ASSERT_EQ( vehicleDataTest.s3_objects().size(), 2 );
    EXPECT_EQ( vehicleDataTest.s3_objects()[0].key(), "abc2" );
    EXPECT_EQ( vehicleDataTest.s3_objects()[1].key(), "abc3" );
#endif
}

} // namespace IoTFleetWise
} // namespace Aws
