// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Schema.h"
#include "AwsIotChannel.h"
#include "AwsIotConnectivityModule.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionSchemeIngestion.h"
#include "CollectionSchemeIngestionList.h"
#include "DecoderManifestIngestion.h"
#include "EnumUtility.h"
#include "ICollectionScheme.h"
#include "IConnectionTypes.h"
#include "IDecoderManifest.h"
#include "ISender.h"
#include "MessageTypes.h"
#include "SenderMock.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "VehicleDataSourceTypes.h"
#include "checkin.pb.h"
#include "collection_schemes.pb.h"
#include "decoder_manifest.pb.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Gt;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::StrictMock;

TEST( CollectionSchemeIngestionTest, CollectionSchemeIngestionClass )
{
    // Create a dummy AwsIotConnectivityModule object so that we can create dummy IReceiver objects to pass to the
    // constructor. Note that the MQTT callback aspect of CollectionSchemeProtoBuilder will not be used in this test.
    std::shared_ptr<AwsIotConnectivityModule> awsIotModule =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );

    Schema collectionSchemeIngestion(
        std::make_shared<AwsIotChannel>( awsIotModule.get(), nullptr, awsIotModule.get()->mConnection ),
        std::make_shared<AwsIotChannel>( awsIotModule.get(), nullptr, awsIotModule.get()->mConnection ),
        std::make_shared<AwsIotChannel>( awsIotModule.get(), nullptr, awsIotModule.get()->mConnection ) );

    auto dummyDecoderManifest = std::make_shared<DecoderManifestIngestion>();
    auto dummyCollectionSchemeList = std::make_shared<CollectionSchemeIngestionList>();
    collectionSchemeIngestion.setDecoderManifest( dummyDecoderManifest );
    collectionSchemeIngestion.setCollectionSchemeList( dummyCollectionSchemeList );

    // For now only check if the previous calls succeed
    ASSERT_TRUE( true );
}

/**
 * @brief CheckinTest class that allows us to run ASSERTs in callback function of the mocked Callback of ISender
 */
class CheckinTest : public ::testing::Test
{
public:
    static void
    checkinTest( const std::string &data, const std::vector<std::string> &sampleDocList, Timestamp timeBeforeCheckin )
    {
        std::shared_ptr<const Clock> clock = ClockHandler::getClock();

        // Create a multiset of ARNS documents we have in a checkin to compare against was was put in the checkin
        std::multiset<std::string> documentSet( sampleDocList.begin(), sampleDocList.end() );

        // Deserialize the protobuf
        Schemas::CheckinMsg::Checkin sentCheckin;
        ASSERT_TRUE( sentCheckin.ParseFromString( data ) );

        // Make sure the size of the documents is the same
        ASSERT_EQ( sentCheckin.document_sync_ids_size(), sampleDocList.size() );

        // Iterate over all the documents found in the checkin
        for ( int i = 0; i < sentCheckin.document_sync_ids_size(); i++ )
        {
            ASSERT_GE( documentSet.count( sentCheckin.document_sync_ids( i ) ), 1 );
            // Erase the entry from the multiset
            documentSet.erase( documentSet.find( sentCheckin.document_sync_ids( i ) ) );
        }

        // Make sure we have erased all the elements from our set
        ASSERT_EQ( documentSet.size(), 0 );

        // Make sure the checkin time is after the time we took at the start of the test
        ASSERT_GE( sentCheckin.timestamp_ms_epoch(), timeBeforeCheckin );
        // Make sure the checkin time is before or equal to this time
        ASSERT_LE( sentCheckin.timestamp_ms_epoch(), clock->systemTimeSinceEpochMs() );
    }
};

TEST( CollectionSchemeIngestionTest, Checkins )
{
    // Create a dummy AwsIotConnectivityModule object so that we can create dummy IReceiver objects
    auto awsIotModule = std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );

    // Create a mock Sender
    auto senderMock = std::make_shared<StrictMock<Testing::SenderMock>>();

    Schema collectionSchemeIngestion(
        std::make_shared<AwsIotChannel>( awsIotModule.get(), nullptr, awsIotModule.get()->mConnection ),
        std::make_shared<AwsIotChannel>( awsIotModule.get(), nullptr, awsIotModule.get()->mConnection ),
        senderMock );

    std::shared_ptr<const Clock> clock = ClockHandler::getClock();
    Timestamp timeBeforeCheckin = clock->systemTimeSinceEpochMs();

    // Create list of Arns
    std::vector<std::string> sampleDocList;

    Sequence seq;
    EXPECT_CALL( *senderMock, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .Times( 3 )
        .InSequence( seq )
        .WillOnce( Return( ConnectivityError::Success ) );
    EXPECT_CALL( *senderMock, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .InSequence( seq )
        .WillOnce( Return( ConnectivityError::NoConnection ) );

    // Test an empty checkin
    ASSERT_TRUE( collectionSchemeIngestion.sendCheckin( sampleDocList ) );
    ASSERT_EQ( senderMock->getSentBufferData().size(), 1 );
    ASSERT_NO_FATAL_FAILURE(
        CheckinTest::checkinTest( senderMock->getSentBufferData()[0].data, sampleDocList, timeBeforeCheckin ) );

    // Add some doc arns
    sampleDocList.emplace_back( "DocArn1" );
    sampleDocList.emplace_back( "DocArn2" );
    sampleDocList.emplace_back( "DocArn3" );
    sampleDocList.emplace_back( "DocArn4" );

    // Test the previous doc list
    ASSERT_TRUE( collectionSchemeIngestion.sendCheckin( sampleDocList ) );

    // Test with duplicates - this shouldn't occur but make sure it works anyways
    sampleDocList.emplace_back( "DocArn4" );
    ASSERT_TRUE( collectionSchemeIngestion.sendCheckin( sampleDocList ) );

    // Second call should simulate a offboardconnectivity issue, the checkin message should fail to send.
    ASSERT_FALSE( collectionSchemeIngestion.sendCheckin( sampleDocList ) );
    ASSERT_EQ( senderMock->getSentBufferData().size(), 4 );
    ASSERT_NO_FATAL_FAILURE(
        CheckinTest::checkinTest( senderMock->getSentBufferData()[3].data, sampleDocList, timeBeforeCheckin ) );
}

/**
 * @brief This test writes a DecoderManifest object to a protobuf binary array. Then it uses this binary array to build
 * a DecoderManifestIngestion object. All the functions of that object are tested against the original proto.
 */
TEST( SchemaTest, DecoderManifestIngestion )
{
#define NODE_A 123
#define NODE_B 4892
#define CAN_SOURCE_FOR_NODE_A 0
#define CAN_SOURCE_FOR_NODE_B 1

    // Create a Decoder manifest protocol buffer and pack it with the data
    Schemas::DecoderManifestMsg::DecoderManifest protoDM;

    protoDM.set_sync_id( "arn:aws:iam::123456789012:user/Development/product_1234/*" );

    // Create a Proto CANSignal
    Schemas::DecoderManifestMsg::CANSignal *protoCANSignalA = protoDM.add_can_signals();

    protoCANSignalA->set_signal_id( 3908 );
    protoCANSignalA->set_interface_id( "123" );
    protoCANSignalA->set_message_id( 600 );
    protoCANSignalA->set_is_big_endian( false );
    protoCANSignalA->set_is_signed( false );
    protoCANSignalA->set_start_bit( 0 );
    protoCANSignalA->set_offset( 100 );
    protoCANSignalA->set_factor( 10 );
    protoCANSignalA->set_length( 8 );

    Schemas::DecoderManifestMsg::CANSignal *protoCANSignalB = protoDM.add_can_signals();

    protoCANSignalB->set_signal_id( 2987 );
    protoCANSignalB->set_interface_id( "123" );
    protoCANSignalB->set_message_id( 600 );
    protoCANSignalB->set_is_big_endian( false );
    protoCANSignalB->set_is_signed( false );
    protoCANSignalB->set_start_bit( 8 );
    protoCANSignalB->set_offset( 100 );
    protoCANSignalB->set_factor( 10 );
    protoCANSignalB->set_length( 8 );

    Schemas::DecoderManifestMsg::CANSignal *protoCANSignalC = protoDM.add_can_signals();

    protoCANSignalC->set_signal_id( 50000 );
    protoCANSignalC->set_interface_id( "4892" );
    protoCANSignalC->set_message_id( 600 );
    protoCANSignalC->set_is_big_endian( false );
    protoCANSignalC->set_is_signed( false );
    protoCANSignalC->set_start_bit( 8 );
    protoCANSignalC->set_offset( 100 );
    protoCANSignalC->set_factor( 10 );
    protoCANSignalC->set_length( 8 );

    Schemas::DecoderManifestMsg::OBDPIDSignal *protoOBDPIDSignalA = protoDM.add_obd_pid_signals();
    protoOBDPIDSignalA->set_signal_id( 123 );
    protoOBDPIDSignalA->set_pid_response_length( 10 );
    protoOBDPIDSignalA->set_service_mode( 1 );
    protoOBDPIDSignalA->set_pid( 0x70 );
    protoOBDPIDSignalA->set_scaling( 1.0 );
    protoOBDPIDSignalA->set_offset( 0.0 );
    protoOBDPIDSignalA->set_start_byte( 0 );
    protoOBDPIDSignalA->set_byte_length( 1 );
    protoOBDPIDSignalA->set_bit_right_shift( 2 );
    protoOBDPIDSignalA->set_bit_mask_length( 2 );

    Schemas::DecoderManifestMsg::OBDPIDSignal *protoOBDPIDSignalB = protoDM.add_obd_pid_signals();
    protoOBDPIDSignalB->set_signal_id( 567 );
    protoOBDPIDSignalB->set_pid_response_length( 4 );
    protoOBDPIDSignalB->set_service_mode( 1 );
    protoOBDPIDSignalB->set_pid( 0x14 );
    protoOBDPIDSignalB->set_scaling( 0.0125 );
    protoOBDPIDSignalB->set_offset( -40.0 );
    protoOBDPIDSignalB->set_start_byte( 2 );
    protoOBDPIDSignalB->set_byte_length( 2 );
    protoOBDPIDSignalB->set_bit_right_shift( 0 );
    protoOBDPIDSignalB->set_bit_mask_length( 8 );

    // Serialize the protocol buffer to a string to avoid malloc with cstyle arrays
    std::string protoSerializedBuffer;

    // Ensure the serialization worked
    ASSERT_TRUE( protoDM.SerializeToString( &protoSerializedBuffer ) );

    // Now we have data to pack our DecoderManifestIngestion object with!
    DecoderManifestIngestion testPIDM;

    // We need a cstyle array to mock data coming from the MQTT IoT core callback. We need to convert the const char*
    // type of the string pointer we get from .data() to const uint8_t*. This is why reinterpret_cast is used.
    testPIDM.copyData( reinterpret_cast<const uint8_t *>( protoSerializedBuffer.data() ),
                       protoSerializedBuffer.length() );

    // This should be false because we just copied the data and it needs to be built first
    ASSERT_FALSE( testPIDM.isReady() );

    // Assert that we get an empty string when we call getID on an object that's not yet built
    ASSERT_EQ( testPIDM.getID(), std::string() );

    ASSERT_TRUE( testPIDM.build() );
    ASSERT_TRUE( testPIDM.isReady() );

    ASSERT_EQ( testPIDM.getID(), protoDM.sync_id() );

    // Get a valid CANMessageFormat
    const CANMessageFormat &testCMF =
        testPIDM.getCANMessageFormat( protoCANSignalA->message_id(), protoCANSignalA->interface_id() );
    ASSERT_TRUE( testCMF.isValid() );

    // Search the CANMessageFormat signals to find the signal format that corresponds to a specific signal
    // Then make sure the data matches the proto DecoderManifest definition of that signal
    int found = 0;
    for ( auto &sigFormat : testCMF.mSignals )
    {
        if ( sigFormat.mSignalID == protoCANSignalA->signal_id() )
        {
            found++;
            ASSERT_EQ( protoCANSignalA->interface_id(),
                       testPIDM.getCANFrameAndInterfaceID( sigFormat.mSignalID ).second );
            ASSERT_EQ( protoCANSignalA->message_id(), testPIDM.getCANFrameAndInterfaceID( sigFormat.mSignalID ).first );
            ASSERT_EQ( protoCANSignalA->is_big_endian(), sigFormat.mIsBigEndian );
            ASSERT_EQ( protoCANSignalA->is_signed(), sigFormat.mIsSigned );
            ASSERT_EQ( protoCANSignalA->start_bit(), sigFormat.mFirstBitPosition );
            ASSERT_EQ( protoCANSignalA->offset(), sigFormat.mOffset );
            ASSERT_EQ( protoCANSignalA->factor(), sigFormat.mFactor );
            ASSERT_EQ( protoCANSignalA->length(), sigFormat.mSizeInBits );
        }
    }
    // Assert that the one signal was found
    ASSERT_EQ( found, 1 );

    // Make sure we get a pair of Invalid CAN and Node Ids, for an signal that the the decoder manifest doesn't have
    ASSERT_EQ( testPIDM.getCANFrameAndInterfaceID( 9999999 ),
               std::make_pair( INVALID_CAN_FRAME_ID, INVALID_CAN_INTERFACE_ID ) );
    ASSERT_EQ( testPIDM.getCANFrameAndInterfaceID( protoCANSignalC->signal_id() ),
               std::make_pair( protoCANSignalC->message_id(), protoCANSignalC->interface_id() ) );

    // Verify OBD-II PID Signals decoder manifest are correctly processed
    auto obdPIDDecoderFormat = testPIDM.getPIDSignalDecoderFormat( 123 );
    ASSERT_EQ( protoOBDPIDSignalA->pid_response_length(), obdPIDDecoderFormat.mPidResponseLength );
    ASSERT_EQ( protoOBDPIDSignalA->service_mode(), toUType( obdPIDDecoderFormat.mServiceMode ) );
    ASSERT_EQ( protoOBDPIDSignalA->pid(), obdPIDDecoderFormat.mPID );
    ASSERT_EQ( protoOBDPIDSignalA->scaling(), obdPIDDecoderFormat.mScaling );
    ASSERT_EQ( protoOBDPIDSignalA->offset(), obdPIDDecoderFormat.mOffset );
    ASSERT_EQ( protoOBDPIDSignalA->start_byte(), obdPIDDecoderFormat.mStartByte );
    ASSERT_EQ( protoOBDPIDSignalA->byte_length(), obdPIDDecoderFormat.mByteLength );
    ASSERT_EQ( protoOBDPIDSignalA->bit_right_shift(), obdPIDDecoderFormat.mBitRightShift );
    ASSERT_EQ( protoOBDPIDSignalA->bit_mask_length(), obdPIDDecoderFormat.mBitMaskLength );

    obdPIDDecoderFormat = testPIDM.getPIDSignalDecoderFormat( 567 );
    ASSERT_EQ( protoOBDPIDSignalB->pid_response_length(), obdPIDDecoderFormat.mPidResponseLength );
    ASSERT_EQ( protoOBDPIDSignalB->service_mode(), toUType( obdPIDDecoderFormat.mServiceMode ) );
    ASSERT_EQ( protoOBDPIDSignalB->pid(), obdPIDDecoderFormat.mPID );
    ASSERT_EQ( protoOBDPIDSignalB->scaling(), obdPIDDecoderFormat.mScaling );
    ASSERT_EQ( protoOBDPIDSignalB->offset(), obdPIDDecoderFormat.mOffset );
    ASSERT_EQ( protoOBDPIDSignalB->start_byte(), obdPIDDecoderFormat.mStartByte );
    ASSERT_EQ( protoOBDPIDSignalB->byte_length(), obdPIDDecoderFormat.mByteLength );
    ASSERT_EQ( protoOBDPIDSignalB->bit_right_shift(), obdPIDDecoderFormat.mBitRightShift );
    ASSERT_EQ( protoOBDPIDSignalB->bit_mask_length(), obdPIDDecoderFormat.mBitMaskLength );

    // There's no signal ID 890, hence this function shall return an INVALID_PID_DECODER_FORMAT
    obdPIDDecoderFormat = testPIDM.getPIDSignalDecoderFormat( 890 );
    ASSERT_EQ( obdPIDDecoderFormat, NOT_FOUND_PID_DECODER_FORMAT );

    ASSERT_EQ( testPIDM.getNetworkProtocol( 3908 ), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( testPIDM.getNetworkProtocol( 2987 ), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( testPIDM.getNetworkProtocol( 50000 ), VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( testPIDM.getNetworkProtocol( 123 ), VehicleDataSourceProtocol::OBD );
    ASSERT_EQ( testPIDM.getNetworkProtocol( 567 ), VehicleDataSourceProtocol::OBD );
}

/**
 * @brief This test writes an invalid DecoderManifest object to a protobuf binary array. The decoder manifest doesn't
 * contain CAN Node, CAN Signal, OBD Signal. When CollectionScheme Ingestion start building, it will return failure due
 * to invalid decoder manifest
 */
TEST( SchemaTest, SchemaInvalidDecoderManifestTest )
{
    // Create a Decoder manifest protocol buffer and pack it with the data
    Schemas::DecoderManifestMsg::DecoderManifest protoDM;

    protoDM.set_sync_id( "arn:aws:iam::123456789012:user/Development/product_1234/*" );

    // Serialize the protocol buffer to a string to avoid malloc with cstyle arrays
    std::string protoSerializedBuffer;

    // Ensure the serialization worked
    ASSERT_TRUE( protoDM.SerializeToString( &protoSerializedBuffer ) );

    // Now we have data to pack our DecoderManifestIngestion object with!
    DecoderManifestIngestion testPIDM;

    // We need a cstyle array to mock data coming from the MQTT IoT core callback. We need to convert the const char*
    // type of the string pointer we get from .data() to const uint8_t*. This is why reinterpret_cast is used.
    testPIDM.copyData( reinterpret_cast<const uint8_t *>( protoSerializedBuffer.data() ),
                       protoSerializedBuffer.length() );

    // This should be false because we just copied the data and it needs to be built first
    ASSERT_FALSE( testPIDM.isReady() );

    // Assert that we get an empty string when we call getID on an unbuilt object
    ASSERT_EQ( testPIDM.getID(), std::string() );

    ASSERT_FALSE( testPIDM.build() );
    ASSERT_FALSE( testPIDM.isReady() );
}

TEST( SchemaTest, CollectionSchemeIngestionList )
{
    // Create our CollectionSchemeIngestionList object or PIPL for short
    CollectionSchemeIngestionList testPIPL;

    // Try to build with no data - this should fail
    ASSERT_FALSE( testPIPL.build() );

    // Try to copy empty data - this should fail
    ASSERT_FALSE( testPIPL.copyData( nullptr, 0 ) );

    // Try using garbage data to copy and build
    std::string garbageString = "This is garbage data";

    // Copy the garbage data and make sure the copy works - copy only fails if no data present
    ASSERT_TRUE(
        testPIPL.copyData( reinterpret_cast<const uint8_t *>( garbageString.data() ), garbageString.length() ) );

    // Try to build with garbage data - this should fail
    ASSERT_FALSE( testPIPL.build() );

    // Now lets try some real data :)
    Schemas::CollectionSchemesMsg::CollectionSchemes protoCollectionSchemesMsg;

    auto p1 = protoCollectionSchemesMsg.add_collection_schemes();
    auto p2 = protoCollectionSchemesMsg.add_collection_schemes();
    auto p3 = protoCollectionSchemesMsg.add_collection_schemes();

    // Make a list of collectionScheme ARNs
    std::vector<std::string> collectionSchemeARNs = { "P1", "P2", "P3" };

    p1->set_campaign_sync_id( collectionSchemeARNs[0] );
    p2->set_campaign_sync_id( collectionSchemeARNs[1] );
    p3->set_campaign_sync_id( collectionSchemeARNs[2] );

    // Serialize the protobuffer to a string to avoid malloc with cstyle arrays
    std::string protoSerializedBuffer;

    // Ensure the serialization worked
    ASSERT_TRUE( protoCollectionSchemesMsg.SerializeToString( &protoSerializedBuffer ) );

    ASSERT_FALSE( testPIPL.isReady() );

    // We need a cstyle array to mock data coming from the MQTT IoT core callback. We need to convert the const char*
    // type of the string pointer we get from .data() to const uint8_t*. This is why reinterpret_cast is used.
    testPIPL.copyData( reinterpret_cast<const uint8_t *>( protoSerializedBuffer.data() ),
                       protoSerializedBuffer.length() );

    // Try to build - this should succeed because we have real data
    ASSERT_TRUE( testPIPL.build() );

    // Make sure the is ready is good to go
    ASSERT_TRUE( testPIPL.isReady() );

    ASSERT_EQ( testPIPL.getCollectionSchemes().size(), 0 );
}

TEST( SchemaTest, CollectionSchemeIngestionHeartBeat )
{
    // Create a  collection scheme Proto Message
    Schemas::CollectionSchemesMsg::CollectionScheme collectionSchemeTestMessage;
    collectionSchemeTestMessage.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1234/*" );
    collectionSchemeTestMessage.set_decoder_manifest_sync_id( "model_manifest_12" );
    collectionSchemeTestMessage.set_start_time_ms_epoch( 1621448160000 );
    collectionSchemeTestMessage.set_expiry_time_ms_epoch( 2621448160000 );

    // Create a Time_based_collection_scheme
    Schemas::CollectionSchemesMsg::TimeBasedCollectionScheme *message1 =
        collectionSchemeTestMessage.mutable_time_based_collection_scheme();
    message1->set_time_based_collection_scheme_period_ms( 5000 );

    collectionSchemeTestMessage.set_after_duration_ms( 0 );
    collectionSchemeTestMessage.set_include_active_dtcs( true );
    collectionSchemeTestMessage.set_persist_all_collected_data( true );
    collectionSchemeTestMessage.set_compress_collected_data( true );
    collectionSchemeTestMessage.set_priority( 9 );

    // Add 3 Signals
    Schemas::CollectionSchemesMsg::SignalInformation *signal1 = collectionSchemeTestMessage.add_signal_information();
    signal1->set_signal_id( 0 );
    signal1->set_sample_buffer_size( 10000 );
    signal1->set_minimum_sample_period_ms( 1000 );
    signal1->set_fixed_window_period_ms( 1000 );
    signal1->set_condition_only_signal( false );

    Schemas::CollectionSchemesMsg::SignalInformation *signal2 = collectionSchemeTestMessage.add_signal_information();
    signal2->set_signal_id( 1 );
    signal2->set_sample_buffer_size( 10000 );
    signal2->set_minimum_sample_period_ms( 1000 );
    signal2->set_fixed_window_period_ms( 1000 );
    signal2->set_condition_only_signal( false );

    Schemas::CollectionSchemesMsg::SignalInformation *signal3 = collectionSchemeTestMessage.add_signal_information();
    signal3->set_signal_id( 2 );
    signal3->set_sample_buffer_size( 1000 );
    signal3->set_minimum_sample_period_ms( 100 );
    signal3->set_fixed_window_period_ms( 100 );
    signal3->set_condition_only_signal( true );

    // Add 2 RAW CAN Messages
    Schemas::CollectionSchemesMsg::RawCanFrame *can1 = collectionSchemeTestMessage.add_raw_can_frames_to_collect();
    can1->set_can_interface_id( "123" );
    can1->set_can_message_id( 0x350 );
    can1->set_sample_buffer_size( 100 );
    can1->set_minimum_sample_period_ms( 10000 );

    Schemas::CollectionSchemesMsg::RawCanFrame *can2 = collectionSchemeTestMessage.add_raw_can_frames_to_collect();
    can2->set_can_interface_id( "124" );
    can2->set_can_message_id( 0x351 );
    can2->set_sample_buffer_size( 10 );
    can2->set_minimum_sample_period_ms( 1000 );

    // Serialize the protocol buffer to a string to avoid malloc with cstyle arrays
    std::string protoSerializedBuffer;

    // Ensure the serialization worked
    ASSERT_TRUE( collectionSchemeTestMessage.SerializeToString( &protoSerializedBuffer ) );

    // Now we have data to pack our DecoderManifestIngestion object with!
    CollectionSchemeIngestion collectionSchemeTest;

    // isReady should evaluate to False
    ASSERT_TRUE( collectionSchemeTest.isReady() == false );

    // Confirm that Message Metadata is not ready as Build has not been called
    ASSERT_FALSE( collectionSchemeTest.getCollectionSchemeID().compare( std::string() ) );
    ASSERT_FALSE( collectionSchemeTest.getDecoderManifestID().compare( std::string() ) );
    ASSERT_TRUE( collectionSchemeTest.getStartTime() == std::numeric_limits<uint64_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getExpiryTime() == std::numeric_limits<uint64_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getAfterDurationMs() == std::numeric_limits<uint32_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.isActiveDTCsIncluded() == false );
    ASSERT_TRUE( collectionSchemeTest.isTriggerOnlyOnRisingEdge() == false );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().size() == 0 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().size() == 0 );
    ASSERT_TRUE( collectionSchemeTest.isPersistNeeded() == false );
    ASSERT_TRUE( collectionSchemeTest.isCompressionNeeded() == false );
    ASSERT_TRUE( collectionSchemeTest.getPriority() == std::numeric_limits<uint32_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getCondition() == nullptr );
    ASSERT_TRUE( collectionSchemeTest.getMinimumPublishIntervalMs() == std::numeric_limits<uint32_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getAllExpressionNodes().size() == 0 );

    // Test for Copy and Build the message
    ASSERT_TRUE( collectionSchemeTest.copyData(
        std::make_shared<Schemas::CollectionSchemesMsg::CollectionScheme>( collectionSchemeTestMessage ) ) );
    ASSERT_TRUE( collectionSchemeTest.build() );

    // isReady should now evaluate to True
    ASSERT_TRUE( collectionSchemeTest.isReady() == true );

    // Confirm that the fields now match the set values in the proto message
    ASSERT_FALSE( collectionSchemeTest.getCollectionSchemeID().compare(
        "arn:aws:iam::2.23606797749:user/Development/product_1234/*" ) );
    ASSERT_FALSE( collectionSchemeTest.getDecoderManifestID().compare( "model_manifest_12" ) );
    ASSERT_TRUE( collectionSchemeTest.getStartTime() == 1621448160000 );
    ASSERT_TRUE( collectionSchemeTest.getExpiryTime() == 2621448160000 );
    ASSERT_TRUE( collectionSchemeTest.getAfterDurationMs() == 0 );
    ASSERT_TRUE( collectionSchemeTest.isActiveDTCsIncluded() == true );
    ASSERT_TRUE( collectionSchemeTest.isTriggerOnlyOnRisingEdge() == false );

    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().size() == 3 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).signalID == 0 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).sampleBufferSize == 10000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).minimumSampleIntervalMs == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).fixedWindowPeriod == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).isConditionOnlySignal == false );

    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).signalID == 1 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).sampleBufferSize == 10000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).minimumSampleIntervalMs == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).fixedWindowPeriod == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).isConditionOnlySignal == false );

    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).signalID == 2 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).sampleBufferSize == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).minimumSampleIntervalMs == 100 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).fixedWindowPeriod == 100 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).isConditionOnlySignal == true );

    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().size() == 2 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).minimumSampleIntervalMs == 10000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).sampleBufferSize == 100 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).frameID == 0x350 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).interfaceID == "123" );

    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 1 ).minimumSampleIntervalMs == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 1 ).sampleBufferSize == 10 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 1 ).frameID == 0x351 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 1 ).interfaceID == "124" );

    ASSERT_TRUE( collectionSchemeTest.isPersistNeeded() == true );
    ASSERT_TRUE( collectionSchemeTest.isCompressionNeeded() == true );
    ASSERT_TRUE( collectionSchemeTest.getPriority() == 9 );
    // For time based collectionScheme the condition is always set to true hence: currentNode.booleanValue=true
    ASSERT_TRUE( collectionSchemeTest.getCondition()->booleanValue == true );
    ASSERT_TRUE( collectionSchemeTest.getCondition()->nodeType == ExpressionNodeType::BOOLEAN );
    // For time based collectionScheme the getMinimumPublishIntervalMs is the same as
    // set_time_based_collection_scheme_period_ms
    ASSERT_TRUE( collectionSchemeTest.getMinimumPublishIntervalMs() == 5000 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().size(), 1 );
}

TEST( SchemaTest, SchemaCollectionEventBased )
{
    // Create a  collection scheme Proto Message
    Schemas::CollectionSchemesMsg::CollectionScheme collectionSchemeTestMessage;
    collectionSchemeTestMessage.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
    collectionSchemeTestMessage.set_decoder_manifest_sync_id( "model_manifest_13" );
    collectionSchemeTestMessage.set_start_time_ms_epoch( 162144816000 );
    collectionSchemeTestMessage.set_expiry_time_ms_epoch( 262144816000 );

    // Create an Event/Condition Based CollectionScheme
    Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme *message =
        collectionSchemeTestMessage.mutable_condition_based_collection_scheme();
    message->set_condition_minimum_interval_ms( 650 );
    message->set_condition_language_version( 20 );
    message->set_condition_trigger_mode(
        Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme_ConditionTriggerMode_TRIGGER_ALWAYS );

    //  Build the AST Tree:
    //----------

    auto *root = new Schemas::CollectionSchemesMsg::ConditionNode();
    message->set_allocated_condition_tree( root );
    auto *rootOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    root->set_allocated_node_operator( rootOp );
    rootOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND );

    //----------

    auto *left = new Schemas::CollectionSchemesMsg::ConditionNode();
    rootOp->set_allocated_left_child( left );
    auto *leftOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    left->set_allocated_node_operator( leftOp );
    leftOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_OR );

    auto *right = new Schemas::CollectionSchemesMsg::ConditionNode();
    rootOp->set_allocated_right_child( right );
    auto *rightOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right->set_allocated_node_operator( rightOp );
    rightOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER );

    //----------

    auto *left_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    leftOp->set_allocated_left_child( left_left );
    auto *left_leftOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    left_left->set_allocated_node_operator( left_leftOp );
    left_leftOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

    auto *left_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    leftOp->set_allocated_right_child( left_right );
    auto *left_rightOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    left_right->set_allocated_node_operator( left_rightOp );
    left_rightOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_NOT_EQUAL );

    auto *right_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    rightOp->set_allocated_left_child( right_left );
    auto *right_leftOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right_left->set_allocated_node_operator( right_leftOp );
    right_leftOp->set_operator_(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER_EQUAL );

    auto *right_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    rightOp->set_allocated_right_child( right_right );
    auto *right_rightOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right_right->set_allocated_node_operator( right_rightOp );
    right_rightOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER );

    //----------

    auto *left_left_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_leftOp->set_allocated_left_child( left_left_left );
    left_left_left->set_node_signal_id( 19 );

    auto *left_left_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_leftOp->set_allocated_right_child( left_left_right );
    left_left_right->set_node_double_value( 1 );

    auto *left_right_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_rightOp->set_allocated_left_child( left_right_left );
    auto *left_right_leftOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    left_right_left->set_allocated_node_operator( left_right_leftOp );
    left_right_leftOp->set_operator_(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MULTIPLY );

    auto *left_right_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_rightOp->set_allocated_right_child( left_right_right );
    auto *left_right_rightOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    left_right_right->set_allocated_node_operator( left_right_rightOp );
    left_right_rightOp->set_operator_(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_DIVIDE );

    auto *right_left_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_leftOp->set_allocated_left_child( right_left_left );
    auto *right_left_leftOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right_left_left->set_allocated_node_operator( right_left_leftOp );
    right_left_leftOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_NOT );

    auto *right_left_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_leftOp->set_allocated_right_child( right_left_right );
    auto *right_left_rightOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right_left_right->set_allocated_node_operator( right_left_rightOp );
    right_left_rightOp->set_operator_(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_PLUS );

    auto *right_right_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_rightOp->set_allocated_left_child( right_right_left );
    auto *right_right_leftOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right_right_left->set_allocated_node_operator( right_right_leftOp );
    right_right_leftOp->set_operator_(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MINUS );

    auto *right_right_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_rightOp->set_allocated_right_child( right_right_right );
    auto *right_right_rightOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    right_right_right->set_allocated_node_operator( right_right_rightOp );
    right_right_rightOp->set_operator_(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MINUS );

    //----------

    auto *left_right_left_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_right_leftOp->set_allocated_left_child( left_right_left_left );
    left_right_left_left->set_node_signal_id( 19 );

    auto *left_right_left_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_right_leftOp->set_allocated_right_child( left_right_left_right );
    left_right_left_right->set_node_double_value( 1 );

    auto *left_right_right_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_right_rightOp->set_allocated_left_child( left_right_right_left );
    left_right_right_left->set_node_signal_id( 19 );

    auto *left_right_right_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    left_right_rightOp->set_allocated_right_child( left_right_right_right );
    left_right_right_right->set_node_double_value( 1 );

    auto *right_left_left_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_left_leftOp->set_allocated_left_child( right_left_left_left );
    right_left_left_left->set_node_signal_id( 19 );

    auto *right_left_right_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_left_rightOp->set_allocated_left_child( right_left_right_left );
    right_left_right_left->set_node_signal_id( 19 );

    auto *right_left_right_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_left_rightOp->set_allocated_right_child( right_left_right_right );
    right_left_right_right->set_node_double_value( 1 );

    auto *right_right_left_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_right_leftOp->set_allocated_left_child( right_right_left_left );
    right_right_left_left->set_node_signal_id( 19 );

    auto *right_right_left_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_right_leftOp->set_allocated_right_child( right_right_left_right );
    right_right_left_right->set_node_double_value( 1 );

    auto *right_right_right_left = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_right_rightOp->set_allocated_left_child( right_right_right_left );
    right_right_right_left->set_node_signal_id( 19 );

    auto *right_right_right_right = new Schemas::CollectionSchemesMsg::ConditionNode();
    right_right_rightOp->set_allocated_right_child( right_right_right_right );
    right_right_right_right->set_node_double_value( 1 );

    //----------

    collectionSchemeTestMessage.set_after_duration_ms( 0 );
    collectionSchemeTestMessage.set_include_active_dtcs( true );
    collectionSchemeTestMessage.set_persist_all_collected_data( true );
    collectionSchemeTestMessage.set_compress_collected_data( true );
    collectionSchemeTestMessage.set_priority( 5 );

    // Add 3 Signals
    Schemas::CollectionSchemesMsg::SignalInformation *signal1 = collectionSchemeTestMessage.add_signal_information();
    signal1->set_signal_id( 19 );
    signal1->set_sample_buffer_size( 5 );
    signal1->set_minimum_sample_period_ms( 500 );
    signal1->set_fixed_window_period_ms( 600 );
    signal1->set_condition_only_signal( true );

    Schemas::CollectionSchemesMsg::SignalInformation *signal2 = collectionSchemeTestMessage.add_signal_information();
    signal2->set_signal_id( 17 );
    signal2->set_sample_buffer_size( 10000 );
    signal2->set_minimum_sample_period_ms( 1000 );
    signal2->set_fixed_window_period_ms( 1000 );
    signal2->set_condition_only_signal( false );

    Schemas::CollectionSchemesMsg::SignalInformation *signal3 = collectionSchemeTestMessage.add_signal_information();
    signal3->set_signal_id( 3 );
    signal3->set_sample_buffer_size( 1000 );
    signal3->set_minimum_sample_period_ms( 100 );
    signal3->set_fixed_window_period_ms( 100 );
    signal3->set_condition_only_signal( true );

    // Add 1 RAW CAN Messages
    Schemas::CollectionSchemesMsg::RawCanFrame *can1 = collectionSchemeTestMessage.add_raw_can_frames_to_collect();
    can1->set_can_interface_id( "1230" );
    can1->set_can_message_id( 0x1FF );
    can1->set_sample_buffer_size( 200 );
    can1->set_minimum_sample_period_ms( 255 );

    // Serialize the protocol buffer to a string to avoid malloc with cstyle arrays
    std::string protoSerializedBuffer;

    // Ensure the serialization worked
    ASSERT_TRUE( collectionSchemeTestMessage.SerializeToString( &protoSerializedBuffer ) );

    // Now we have data to pack our DecoderManifestIngestion object with!
    CollectionSchemeIngestion collectionSchemeTest;

    // isReady should evaluate to False
    ASSERT_TRUE( collectionSchemeTest.isReady() == false );

    // Confirm that Message Metadata is not ready as Build has not been called
    ASSERT_FALSE( collectionSchemeTest.getCollectionSchemeID().compare( std::string() ) );
    ASSERT_FALSE( collectionSchemeTest.getDecoderManifestID().compare( std::string() ) );
    ASSERT_TRUE( collectionSchemeTest.getStartTime() == std::numeric_limits<uint64_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getExpiryTime() == std::numeric_limits<uint64_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getAfterDurationMs() == std::numeric_limits<uint32_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.isActiveDTCsIncluded() == false );
    ASSERT_TRUE( collectionSchemeTest.isTriggerOnlyOnRisingEdge() == false );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().size() == 0 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().size() == 0 );
    ASSERT_TRUE( collectionSchemeTest.isPersistNeeded() == false );
    ASSERT_TRUE( collectionSchemeTest.isCompressionNeeded() == false );
    ASSERT_TRUE( collectionSchemeTest.getPriority() == std::numeric_limits<uint32_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getCondition() == nullptr );
    ASSERT_TRUE( collectionSchemeTest.getMinimumPublishIntervalMs() == std::numeric_limits<uint32_t>::max() );
    ASSERT_TRUE( collectionSchemeTest.getAllExpressionNodes().size() == 0 );

    // Test for Copy and Build the message
    ASSERT_TRUE( collectionSchemeTest.copyData(
        std::make_shared<Schemas::CollectionSchemesMsg::CollectionScheme>( collectionSchemeTestMessage ) ) );
    ASSERT_TRUE( collectionSchemeTest.build() );

    // isReady should now evaluate to True
    ASSERT_TRUE( collectionSchemeTest.isReady() == true );

    // Confirm that the fields now match the set values in the proto message
    ASSERT_FALSE( collectionSchemeTest.getCollectionSchemeID().compare(
        "arn:aws:iam::2.23606797749:user/Development/product_1235/*" ) );
    ASSERT_FALSE( collectionSchemeTest.getDecoderManifestID().compare( "model_manifest_13" ) );
    ASSERT_TRUE( collectionSchemeTest.getStartTime() == 162144816000 );
    ASSERT_TRUE( collectionSchemeTest.getExpiryTime() == 262144816000 );
    ASSERT_TRUE( collectionSchemeTest.getAfterDurationMs() == 0 );
    ASSERT_TRUE( collectionSchemeTest.isActiveDTCsIncluded() == true );
    ASSERT_TRUE( collectionSchemeTest.isTriggerOnlyOnRisingEdge() == false );
    // Signals
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().size() == 3 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).signalID == 19 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).sampleBufferSize == 5 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).minimumSampleIntervalMs == 500 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).fixedWindowPeriod == 600 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 0 ).isConditionOnlySignal == true );

    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).signalID == 17 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).sampleBufferSize == 10000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).minimumSampleIntervalMs == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).fixedWindowPeriod == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 1 ).isConditionOnlySignal == false );

    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).signalID == 3 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).sampleBufferSize == 1000 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).minimumSampleIntervalMs == 100 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).fixedWindowPeriod == 100 );
    ASSERT_TRUE( collectionSchemeTest.getCollectSignals().at( 2 ).isConditionOnlySignal == true );

    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().size() == 1 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).minimumSampleIntervalMs == 255 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).sampleBufferSize == 200 );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).frameID == 0x1FF );
    ASSERT_TRUE( collectionSchemeTest.getCollectRawCanFrames().at( 0 ).interfaceID == "1230" );

    ASSERT_TRUE( collectionSchemeTest.isPersistNeeded() == true );
    ASSERT_TRUE( collectionSchemeTest.isCompressionNeeded() == true );
    ASSERT_TRUE( collectionSchemeTest.getPriority() == 5 );

    // For Event based collectionScheme the getMinimumPublishIntervalMs is the same as condition_minimum_interval_ms
    ASSERT_TRUE( collectionSchemeTest.getMinimumPublishIntervalMs() == 650 );

    // Verify the AST
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().size(), 52 );
    //----------
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).nodeType,
               ExpressionNodeType::OPERATOR_LOGICAL_AND );
    //----------
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->nodeType,
               ExpressionNodeType::OPERATOR_LOGICAL_OR );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->nodeType,
               ExpressionNodeType::OPERATOR_SMALLER );
    //----------
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->left->nodeType,
               ExpressionNodeType::OPERATOR_BIGGER );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->nodeType,
               ExpressionNodeType::OPERATOR_NOT_EQUAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->nodeType,
               ExpressionNodeType::OPERATOR_SMALLER_EQUAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->nodeType,
               ExpressionNodeType::OPERATOR_SMALLER );
    //----------
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->left->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->left->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->left->right->nodeType,
               ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->left->right->floatingValue, 1 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->left->nodeType,
               ExpressionNodeType::OPERATOR_ARITHMETIC_MULTIPLY );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->right->nodeType,
               ExpressionNodeType::OPERATOR_ARITHMETIC_DIVIDE );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->left->nodeType,
               ExpressionNodeType::OPERATOR_LOGICAL_NOT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->right->nodeType,
               ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->left->nodeType,
               ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->right->nodeType,
               ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS );
    //----------
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->left->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->left->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->left->right->nodeType,
               ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->left->right->floatingValue, 1 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->right->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->right->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->right->right->nodeType,
               ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->right->right->right->floatingValue, 1 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->left->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->left->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->left->right, nullptr );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->right->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->right->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->right->right->nodeType,
               ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->left->right->right->floatingValue, 1 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->left->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->left->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->left->right->nodeType,
               ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->left->right->floatingValue, 1 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->right->left->nodeType,
               ExpressionNodeType::SIGNAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->right->left->signalID, 19 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->right->right->nodeType,
               ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->right->right->right->floatingValue, 1 );
    //----------
    ASSERT_TRUE( collectionSchemeTest.getCondition()->booleanValue == false );
}

TEST( SchemaTest, SchemaGeohashFunctionNode )
{
    // Create a  collection scheme Proto Message
    Schemas::CollectionSchemesMsg::CollectionScheme collectionSchemeTestMessage;
    collectionSchemeTestMessage.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
    collectionSchemeTestMessage.set_decoder_manifest_sync_id( "model_manifest_13" );
    collectionSchemeTestMessage.set_start_time_ms_epoch( 162144816000 );
    collectionSchemeTestMessage.set_expiry_time_ms_epoch( 262144816000 );

    // Create an Event/Condition Based CollectionScheme
    Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme *message =
        collectionSchemeTestMessage.mutable_condition_based_collection_scheme();
    message->set_condition_minimum_interval_ms( 650 );
    message->set_condition_language_version( 20 );
    message->set_condition_trigger_mode(
        Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme_ConditionTriggerMode_TRIGGER_ALWAYS );

    // Build a simple AST Tree.
    // Root: Equal
    // Left Child: GeohashFunction
    // Right Child: 1.0
    auto *root = new Schemas::CollectionSchemesMsg::ConditionNode();
    auto *rootOp = new Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator();
    rootOp->set_operator_( Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_EQUAL );

    auto *leftChild = new Schemas::CollectionSchemesMsg::ConditionNode();
    auto *leftChildFunction = new Schemas::CollectionSchemesMsg::ConditionNode_NodeFunction();
    auto *leftChildGeohashFunction = new Schemas::CollectionSchemesMsg::ConditionNode_NodeFunction_GeohashFunction();
    leftChildGeohashFunction->set_latitude_signal_id( 0x1 );
    leftChildGeohashFunction->set_longitude_signal_id( 0x2 );
    leftChildGeohashFunction->set_geohash_precision( 6 );
    leftChildGeohashFunction->set_gps_unit(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeFunction_GeohashFunction_GPSUnitType_MILLIARCSECOND );

    auto *rightChild = new Schemas::CollectionSchemesMsg::ConditionNode();
    rightChild->set_node_double_value( 1.0 );

    leftChildFunction->set_allocated_geohash_function( leftChildGeohashFunction );
    leftChild->set_allocated_node_function( leftChildFunction );
    // connect to root
    rootOp->set_allocated_right_child( rightChild );
    rootOp->set_allocated_left_child( leftChild );
    root->set_allocated_node_operator( rootOp );
    message->set_allocated_condition_tree( root );

    // Serialize the protocol buffer to a string to avoid malloc with cstyle arrays
    std::string protoSerializedBuffer;

    // Ensure the serialization worked
    ASSERT_TRUE( collectionSchemeTestMessage.SerializeToString( &protoSerializedBuffer ) );

    // Now we have data to pack our DecoderManifestIngestion object with!
    CollectionSchemeIngestion collectionSchemeTest;

    // isReady should evaluate to False
    ASSERT_TRUE( collectionSchemeTest.isReady() == false );

    // Test for Copy and Build the message
    ASSERT_TRUE( collectionSchemeTest.copyData(
        std::make_shared<Schemas::CollectionSchemesMsg::CollectionScheme>( collectionSchemeTestMessage ) ) );
    ASSERT_TRUE( collectionSchemeTest.build() );

    // isReady should now evaluate to True
    ASSERT_TRUE( collectionSchemeTest.isReady() == true );

    // Verify the AST
    // Verify Left Side
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().size(), 6 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).nodeType, ExpressionNodeType::OPERATOR_EQUAL );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->nodeType,
               ExpressionNodeType::GEOHASHFUNCTION );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->function.geohashFunction.latitudeSignalID,
               0x01 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->function.geohashFunction.longitudeSignalID,
               0x02 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->function.geohashFunction.precision, 6 );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).left->function.geohashFunction.gpsUnitType,
               GeohashFunction::GPSUnitType::MILLIARCSECOND );

    // Verify Right Side
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->nodeType, ExpressionNodeType::FLOAT );
    ASSERT_EQ( collectionSchemeTest.getAllExpressionNodes().at( 0 ).right->floatingValue, 1.0 );
}

} // namespace IoTFleetWise
} // namespace Aws
