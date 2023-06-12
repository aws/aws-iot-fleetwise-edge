// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataCollectionSender.h"
#include <boost/filesystem.hpp>
#include <functional>
#include <gtest/gtest.h>
#include <list>

using namespace Aws::IoTFleetWise::DataManagement;

class MockSender : public ISender
{
public:
    using Callback = std::function<ConnectivityError( const std::uint8_t *buf, size_t size )>;
    Callback mCallback;

    bool
    isAlive()
    {
        return true;
    }

    size_t
    getMaxSendSize() const
    {
        return 128U * 1024U;
    }

    ConnectivityError
    sendBuffer( const std::uint8_t *buf,
                size_t size,
                struct Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams collectionSchemeParams =
                    CollectionSchemeParams() ) override
    {
        static_cast<void>( collectionSchemeParams ); // Currently not implemented, hence unused

        if ( !mCallback )
        {
            return ConnectivityError::NoConnection;
        }
        return mCallback( buf, size );
    }
};

class DataCollectionSenderTest : public ::testing::Test
{
public:
    void
    checkProto( const std::uint8_t *buf, size_t size )
    {
        std::string expectedProto;
        for ( size_t i = 0U; i < size; i++ )
        {
            expectedProto += static_cast<char>( buf[i] );
        }
        VehicleDataMsg::VehicleData vehicleDataTest{};

        ASSERT_TRUE( vehicleDataTest.ParseFromString( expectedProto ) );

        /* Read and compare to written fields */
        ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
        ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
        ASSERT_EQ( 800, vehicleDataTest.collection_event_time_ms_epoch() );
        ASSERT_EQ( vehicleDataTest.captured_signals_size(), 3 );
        ASSERT_EQ( vehicleDataTest.can_frames_size(), 3 );
        auto dtcData = vehicleDataTest.mutable_dtc_data();
        ASSERT_EQ( dtcData->relative_time_ms(), 1200 );
        ASSERT_EQ( dtcData->active_dtc_codes_size(), 2 );
        auto geohashProto = vehicleDataTest.mutable_geohash();
        ASSERT_EQ( geohashProto->geohash_string(), "9q9hwg28j" );
        ASSERT_EQ( geohashProto->prev_reported_geohash_string(), "9q9hwg281" );
    }

    void
    checkProtoForMaxMessages( const std::uint8_t *buf, size_t size, uint32_t maxMessageCount )
    {
        std::string expectedProto;
        for ( size_t i = 0U; i < size; i++ )
        {
            expectedProto += static_cast<char>( buf[i] );
        }
        VehicleDataMsg::VehicleData vehicleDataTest{};

        ASSERT_TRUE( vehicleDataTest.ParseFromString( expectedProto ) );

        /* Read and compare to written fields */
        ASSERT_EQ( "123", vehicleDataTest.campaign_sync_id() );
        ASSERT_EQ( "456", vehicleDataTest.decoder_sync_id() );
        ASSERT_EQ( 800, vehicleDataTest.collection_event_time_ms_epoch() );
        // Number of messages should always be less than or equal to the transmit threshold specified in config
        auto dtcData = vehicleDataTest.mutable_dtc_data();
        ASSERT_LE( vehicleDataTest.can_frames_size() + vehicleDataTest.captured_signals_size() +
                       dtcData->active_dtc_codes_size(),
                   maxMessageCount );
    }

    std::shared_ptr<TriggeredCollectionSchemeData> collectedDataPtr;
    DataCollectionSenderTest()
    {
        collectedDataPtr = std::make_shared<TriggeredCollectionSchemeData>();
        collectedDataPtr->metaData.collectionSchemeID = "123";
        collectedDataPtr->metaData.decoderID = "456";
        collectedDataPtr->triggerTime = 800;
        {
            CollectedSignal collectedSignalMsg1( 120 /*signalId*/, 800 /*receiveTime*/, 77.88 /*value*/ );
            collectedDataPtr->signals.push_back( collectedSignalMsg1 );
            CollectedSignal collectedSignalMsg2( 10 /*signalId*/, 1000 /*receiveTime*/, 46.5 /*value*/ );
            collectedDataPtr->signals.push_back( collectedSignalMsg2 );
            CollectedSignal collectedSignalMsg3( 12 /*signalId*/, 1200 /*receiveTime*/, 98.9 /*value*/ );
            collectedDataPtr->signals.push_back( collectedSignalMsg3 );
        }
        {
            std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
            CollectedCanRawFrame canFrames1( 12 /*frameId*/, 1 /*nodeId*/, 815 /*receiveTime*/, data, sizeof data );
            collectedDataPtr->canFrames.push_back( canFrames1 );
            CollectedCanRawFrame canFrames2( 4 /*frameId*/, 2 /*nodeId*/, 1100 /*receiveTime*/, data, sizeof data );
            collectedDataPtr->canFrames.push_back( canFrames2 );
            CollectedCanRawFrame canFrames3( 6 /*frameId*/, 3 /*nodeId*/, 1300 /*receiveTime*/, data, sizeof data );
            collectedDataPtr->canFrames.push_back( canFrames3 );
        }
        {
            collectedDataPtr->mDTCInfo.mSID = SID::STORED_DTC;
            collectedDataPtr->mDTCInfo.receiveTime = 2000;
            collectedDataPtr->mDTCInfo.mDTCCodes = { "U0123", "P0456" };
        }
        {
            collectedDataPtr->mGeohashInfo.mGeohashString = "9q9hwg28j";
            collectedDataPtr->mGeohashInfo.mPrevReportedGeohashString = "9q9hwg281";
        }
    }

protected:
    boost::filesystem::path mTmpDir = boost::filesystem::temp_directory_path();
};

TEST_F( DataCollectionSenderTest, TestMaxMessageCountNotHit )
{
    auto mockSender = std::make_shared<MockSender>();
    CANInterfaceIDTranslator canIDTranslator;
    DataCollectionSender dataCollectionSender( mockSender, 10, canIDTranslator );

    mockSender->mCallback = [&]( const std::uint8_t *buf, size_t size ) -> ConnectivityError {
        // deserialize the proto
        checkProto( buf, size );
        return ConnectivityError::Success;
    };

    dataCollectionSender.send( collectedDataPtr );
}

TEST_F( DataCollectionSenderTest, TestMaxMessageCountHit )
{
    auto mockSender = std::make_shared<MockSender>();
    CANInterfaceIDTranslator canIDTranslator;
    DataCollectionSender dataCollectionSender( mockSender, 2, canIDTranslator );

    uint32_t maxMessageCount = 2;
    mockSender->mCallback = [&]( const std::uint8_t *buf, size_t size ) -> ConnectivityError {
        checkProtoForMaxMessages( buf, size, maxMessageCount );
        return ConnectivityError::Success;
    };

    dataCollectionSender.send( collectedDataPtr );
}

TEST_F( DataCollectionSenderTest, TestTransmitPayload )
{
    auto mockSender = std::make_shared<MockSender>();
    CANInterfaceIDTranslator canIDTranslator;
    DataCollectionSender dataCollectionSender( mockSender, 10, canIDTranslator );

    std::string testProto = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";

    mockSender->mCallback = [&]( const std::uint8_t *buf, size_t size ) -> ConnectivityError {
        static_cast<void>( buf );
        static_cast<void>( size );
        return ConnectivityError::Success;
    };
    ASSERT_EQ( dataCollectionSender.transmit( testProto ), ConnectivityError::Success );
}
