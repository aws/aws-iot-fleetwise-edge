// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include "SenderMock.h"
#include "Testing.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CollectionSchemeIngestion.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamManager.h"
#include "collection_schemes.pb.h"
#include <boost/filesystem.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;
using ::testing::WithArg;
using ::testing::WithArgs;

class StreamForwarderTest : public ::testing::Test
{
protected:
    boost::filesystem::path mPersistenceRootDir;
    StrictMock<Testing::SenderMock> mMqttSender;
    PayloadAdaptionConfig mPayloadAdaptionConfigUncompressed{ 80, 70, 90, 10 };
    PayloadAdaptionConfig mPayloadAdaptionConfigCompressed{ 80, 70, 90, 10 };
    CANInterfaceIDTranslator mCANIDTranslator;
    TelemetryDataSender mTelemetryDataSender;
    Store::StreamManager mStreamManager;
    RateLimiter mRateLimiter;
    Store::StreamForwarder mStreamForwarder;
    std::string mTelemetryDataTopic = "$aws/iotfleetwise/vehicles/thing-name/signals";
    std::shared_ptr<const Clock> mClock;
    static constexpr unsigned MAXIMUM_PAYLOAD_SIZE = 400;

    struct CollectedSignals
    {
        Store::CampaignID campaignID;
        TriggeredCollectionSchemeData data = {};
        std::map<Store::PartitionID, std::vector<CollectedSignal>> signals;
    };

    StreamForwarderTest()
        : mPersistenceRootDir( getTempDir() )
        , mTelemetryDataSender(
              [this]() -> ISender & {
                  EXPECT_CALL( mMqttSender, getMaxSendSize() )
                      .Times( AnyNumber() )
                      .WillRepeatedly( Return( MAXIMUM_PAYLOAD_SIZE ) );
                  return mMqttSender;
              }(),
              std::make_unique<DataSenderProtoWriter>( mCANIDTranslator, nullptr ),
              mPayloadAdaptionConfigUncompressed,
              mPayloadAdaptionConfigCompressed )
        , mStreamManager(
              mPersistenceRootDir.string(), std::make_unique<DataSenderProtoWriter>( mCANIDTranslator, nullptr ), 0 )
        , mStreamForwarder( mStreamManager, mTelemetryDataSender, mRateLimiter, 10 )
    {
    }

    void
    SetUp() override
    {
        mClock = ClockHandler::getClock();
        mCANIDTranslator.add( "can123" );
        ON_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, _, _ ) )
            .WillByDefault( InvokeArgument<3>( ConnectivityError::Success ) );
    }

    void
    TearDown() override
    {
        mStreamForwarder.stop();
        boost::filesystem::remove_all( mPersistenceRootDir );
    }

    static Aws::IoTFleetWise::ActiveCollectionSchemes
    getMultipleCampaignsWithMultiplePartitions()
    {
        Aws::IoTFleetWise::ActiveCollectionSchemes out;

        Schemas::CollectionSchemesMsg::CollectionScheme campaign1;
        {
            Schemas::CollectionSchemesMsg::CollectionScheme scheme;
            scheme.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/campaign1" );
            scheme.set_campaign_arn( "arn:aws:iam::2.23606797749:user/campaign1" );
            scheme.set_decoder_manifest_sync_id( "model_manifest_13" );

            auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

            auto *partition = store_and_forward_configuration->add_partition_configuration();

            // partition 0
            auto *storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( 1000000 );
            storageOptions->set_storage_location( "partition0" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );
            // partition 1
            partition = store_and_forward_configuration->add_partition_configuration();
            storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( 1000000 );
            storageOptions->set_storage_location( "partition1" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

            // map signals to partitions
            auto *signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 0 );
            signalInformation->set_data_partition_id( 0 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 1 );
            signalInformation->set_data_partition_id( 0 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 2 );
            signalInformation->set_data_partition_id( 1 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 3 );
            signalInformation->set_data_partition_id( 1 );

            campaign1 = scheme;
        }

        Schemas::CollectionSchemesMsg::CollectionScheme campaign2;
        {
            Schemas::CollectionSchemesMsg::CollectionScheme scheme;
            scheme.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/campaign2" );
            scheme.set_campaign_arn( "arn:aws:iam::2.23606797749:user/campaign2" );
            scheme.set_decoder_manifest_sync_id( "model_manifest_13" );

            auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

            auto *partition = store_and_forward_configuration->add_partition_configuration();

            // partition 0
            auto *storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( 1000000 );
            storageOptions->set_storage_location( "partition0" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );
            // partition 1
            partition = store_and_forward_configuration->add_partition_configuration();
            storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( 1000000 );
            storageOptions->set_storage_location( "partition1" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

            // map signals to partitions
            auto *signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 4 );
            signalInformation->set_data_partition_id( 0 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 5 );
            signalInformation->set_data_partition_id( 0 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 6 );
            signalInformation->set_data_partition_id( 1 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 7 );
            signalInformation->set_data_partition_id( 1 );

            campaign2 = scheme;
        }

        convertSchemes( { campaign1, campaign2 }, out );
        return out;
    }

    inline DTCInfo
    fakeDtcInfo()
    {
        DTCInfo info{};
        info.receiveTime = mClock->systemTimeSinceEpochMs();
        info.mSID = SID::TESTING;
        info.mDTCCodes.emplace_back( "code" );
        return info;
    }

    static void
    convertSchemes( std::vector<Schemas::CollectionSchemesMsg::CollectionScheme> schemes,
                    Aws::IoTFleetWise::ActiveCollectionSchemes &convertedSchemes )
    {
        Aws::IoTFleetWise::ActiveCollectionSchemes activeSchemes;
        for ( auto scheme : schemes )
        {
            auto campaign = std::make_shared<CollectionSchemeIngestion>(
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                std::make_shared<CollectionSchemeIngestion::PartialSignalIDLookup>()
#endif
            );
            campaign->copyData( std::make_shared<Schemas::CollectionSchemesMsg::CollectionScheme>( scheme ) );
            ASSERT_TRUE( campaign->build() );
            activeSchemes.activeCollectionSchemes.emplace_back( campaign );
        }
        convertedSchemes = activeSchemes;
    }

    void
    storeAll( std::vector<CollectedSignals> collectedSignals )
    {
        for ( auto signal : collectedSignals )
        {
            ASSERT_EQ( mStreamManager.appendToStreams( signal.data ), Store::StreamManager::ReturnCode::SUCCESS );
        }
    }

    void
    forwardAll( std::vector<CollectedSignals> collectedSignals )
    {
        for ( auto signals : collectedSignals )
        {
            for ( auto entry : signals.signals )
            {
                mStreamForwarder.beginForward(
                    signals.campaignID, entry.first, Aws::IoTFleetWise::Store::StreamForwarder::Source::CONDITION );
            }
        }
    }

    void
    forwardAllIoTJob( std::vector<CollectedSignals> collectedSignals, uint64_t endTime )
    {
        for ( auto signals : collectedSignals )
        {
            mStreamForwarder.beginJobForward( signals.campaignID, endTime );
        }
    }

    std::vector<CollectedSignals>
    buildTestData( std::shared_ptr<Aws::IoTFleetWise::ActiveCollectionSchemes> campaign, Timestamp triggerTime = 0 )
    {
        std::vector<CollectedSignals> collectedSignals;
        for ( auto scheme : campaign->activeCollectionSchemes )
        {
            CollectedSignals signals;
            signals.campaignID = scheme->getCampaignArn();

            signals.data.eventID = 1234;
            signals.data.metadata.collectionSchemeID = scheme->getCollectionSchemeID();
            signals.data.metadata.campaignArn = signals.campaignID;
            signals.data.mDTCInfo = fakeDtcInfo();
            for ( auto signal : scheme->getCollectSignals() )
            {
                CollectedSignal collectedSignal = {
                    signal.signalID, mClock->systemTimeSinceEpochMs(), 5, SignalType::UINT8 };
                signals.signals[signal.dataPartitionId].emplace_back( collectedSignal );
                signals.data.signals.emplace_back( collectedSignal );
                signals.data.triggerTime = triggerTime;
            }
            collectedSignals.emplace_back( signals );
        }
        return collectedSignals;
    }
};

TEST_F( StreamForwarderTest, StoreAndForwardDataFromMultipleCampaignsAndPartitions )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    storeAll( collectedSignals );
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    forwardAll( collectedSignals );

    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 4U );
}

TEST_F( StreamForwarderTest, ForwardStopsForPartitionWhenRequested )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    forwardAll( collectedSignals );
    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 4U );

    // stop uploading from one of the campaigns/partitions
    mStreamForwarder.cancelForward( campaign->activeCollectionSchemes[0]->getCampaignArn(),
                                    0,
                                    Aws::IoTFleetWise::Store::StreamForwarder::Source::CONDITION );
    mMqttSender.clearSentBufferData();
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 3 );
    storeAll( collectedSignals );
    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 3U );
}

TEST_F( StreamForwarderTest, ForwardStopsForCampaignsThatAreRemoved )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    storeAll( collectedSignals );

    // remove the campaign entirely
    mStreamManager.onChangeCollectionSchemeList( {} );

    forwardAll( collectedSignals );

    DELAY_ASSERT_TRUE( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size() == 0U );
}

TEST_F( StreamForwarderTest, StoreAndForwardDataFromMultipleCampaignsAndPartitionsIoTJobs )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    storeAll( collectedSignals );
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    forwardAllIoTJob( collectedSignals, 0 );

    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 4U );
}

TEST_F( StreamForwarderTest, ForwardStopsForIoTJobWhenEndOfStream )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    forwardAllIoTJob( collectedSignals, 0 );
    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 4U );
    std::this_thread::sleep_for(
        std::chrono::milliseconds( 1000 ) ); // give thread time to update mJobCampaignToPartitions

    mMqttSender.clearSentBufferData();
    // End of Stream is hit so forwarding has stopped
    storeAll( collectedSignals );
    DELAY_ASSERT_TRUE( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size() == 0U );
}

TEST_F( StreamForwarderTest, ForwardAgainWhenAPayloadFailsToBeUploaded )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );

    {
        InSequence seq;
        // Make the first upload to fail
        EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) )
            .WillOnce( InvokeArgument<3>( ConnectivityError::TransmissionError ) );
        EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    }
    forwardAllIoTJob( collectedSignals, 0 );
    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 5U );
    std::this_thread::sleep_for(
        std::chrono::milliseconds( 1000 ) ); // give thread time to update mJobCampaignToPartitions

    mMqttSender.clearSentBufferData();
    // End of Stream is hit so forwarding has stopped
    storeAll( collectedSignals );
    DELAY_ASSERT_TRUE( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size() == 0U );
}

TEST_F( StreamForwarderTest, ForwardWhenBothIotJobAndConditionAreActive )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    forwardAllIoTJob( collectedSignals, 0 );
    forwardAll( collectedSignals );
    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 4U );
    std::this_thread::sleep_for(
        std::chrono::milliseconds( 1000 ) ); // give thread time to update mJobCampaignToPartitions

    // End of Stream is hit so forwarding has stopped for IOT_JOB
    // stop uploading from one of the campaigns/partitions
    mStreamForwarder.cancelForward( campaign->activeCollectionSchemes[0]->getCampaignArn(),
                                    0,
                                    Aws::IoTFleetWise::Store::StreamForwarder::Source::CONDITION );
    mMqttSender.clearSentBufferData();
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 3 );
    storeAll( collectedSignals );
    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 3U );
}

TEST_F( StreamForwarderTest, ForwarderStopsForIoTJobWhenEndTime )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( getMultipleCampaignsWithMultiplePartitions() );
    mStreamManager.onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    // isolate the endTime
    auto endTime = mClock->systemTimeSinceEpochMs();

    // build same test data but with collection triggertime after the endTime
    auto collectedSignals2 = buildTestData( campaign, endTime + 1 );

    auto combinedSignals = collectedSignals;
    combinedSignals.insert( combinedSignals.end(), collectedSignals2.begin(), collectedSignals2.end() );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder.start() );
    ASSERT_TRUE( mStreamForwarder.isAlive() );

    storeAll( combinedSignals );
    // endTime will be after the collectedSignals collection trigger time, but before the collectedSignals2 collection
    // trigger time
    EXPECT_CALL( mMqttSender, mockedSendBuffer( mTelemetryDataTopic, _, Gt( 0 ), _ ) ).Times( 4 );
    forwardAllIoTJob( combinedSignals, endTime );

    WAIT_ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( mTelemetryDataTopic ).size(), 4U );
}
} // namespace IoTFleetWise
} // namespace Aws
