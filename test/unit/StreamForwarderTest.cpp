// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StreamForwarder.h"
#include "CANDataTypes.h"
#include "CANInterfaceIDTranslator.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionSchemeIngestion.h"
#include "DataSenderProtoReader.h"
#include "DataSenderProtoWriter.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "IConnectionTypes.h"
#include "OBDDataTypes.h"
#include "RateLimiter.h"
#include "SenderMock.h"
#include "SignalTypes.h"
#include "StreamManager.h"
#include "TelemetryDataSender.h"
#include "Testing.h"
#include "TimeTypes.h"
#include "WaitUntil.h"
#include <array>
#include <atomic>
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
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;
using ::testing::WithArg;
using ::testing::WithArgs;

class StreamForwarderTest : public ::testing::Test
{
protected:
    std::shared_ptr<Store::StreamManager> mStreamManager;
    std::shared_ptr<Store::StreamForwarder> mStreamForwarder;
    std::shared_ptr<TelemetryDataSender> mTelemetryDataSender;
    std::shared_ptr<NiceMock<Testing::SenderMock>> mMqttSender;
    std::shared_ptr<RateLimiter> mRateLimiter;
    boost::filesystem::path mPersistenceRootDir;
    CANInterfaceIDTranslator mCANIDTranslator;
    std::shared_ptr<const Clock> mClock;
    static constexpr unsigned MAXIMUM_PAYLOAD_SIZE = 400;
    PayloadAdaptionConfig mPayloadAdaptionConfigUncompressed{ 80, 70, 90, 10 };
    PayloadAdaptionConfig mPayloadAdaptionConfigCompressed{ 80, 70, 90, 10 };

    struct CollectedSignals
    {
        Store::CampaignID campaignID = {};
        TriggeredCollectionSchemeData data = {};
        std::map<Store::PartitionID, std::vector<CollectedSignal>> signals = {};
    };

    void
    SetUp() override
    {
        mClock = ClockHandler::getClock();
        mCANIDTranslator.add( "can123" );
        mPersistenceRootDir = getTempDir();
        auto protoWriter = std::make_shared<DataSenderProtoWriter>( mCANIDTranslator, nullptr );
        auto protoReader = std::make_shared<DataSenderProtoReader>( mCANIDTranslator );
        mStreamManager = std::make_shared<Store::StreamManager>( mPersistenceRootDir.string(), protoWriter, 0 );
        mMqttSender = std::make_shared<NiceMock<Testing::SenderMock>>();
        EXPECT_CALL( *mMqttSender, getMaxSendSize() )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( MAXIMUM_PAYLOAD_SIZE ) );
        mTelemetryDataSender = std::make_shared<TelemetryDataSender>(
            mMqttSender, protoWriter, mPayloadAdaptionConfigUncompressed, mPayloadAdaptionConfigCompressed );
        mRateLimiter = std::make_shared<RateLimiter>();
        auto streamForwarderIdleTime = 10;
        mStreamForwarder = std::make_shared<Store::StreamForwarder>(
            mStreamManager, mTelemetryDataSender, mRateLimiter, streamForwarderIdleTime );
    }

    void
    TearDown() override
    {
        mStreamForwarder->stop();
        boost::filesystem::remove_all( mPersistenceRootDir );
    }

    void
    trackNumMqttMessagesSent( std::atomic<int> &messagesSent )
    {
        EXPECT_CALL( *mMqttSender, isAlive() ).Times( AnyNumber() ).WillRepeatedly( Return( true ) );
        ON_CALL( *mMqttSender, mockedSendBuffer( _, _, _, _ ) )
            .WillByDefault( DoAll( InvokeWithoutArgs( [&]() {
                                       messagesSent++;
                                       // Sleep to allow the main thread to now cancel or update the forwarding config
                                       std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
                                   } ),
                                   InvokeArgument<3>( ConnectivityError::Success ) ) );
    }

    Aws::IoTFleetWise::ActiveCollectionSchemes
    GetMultipleCampaignsWithMultiplePartitions()
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
            scheme.add_raw_can_frames_to_collect();
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 1 );
            signalInformation->set_data_partition_id( 0 );
            scheme.add_raw_can_frames_to_collect();
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 2 );
            signalInformation->set_data_partition_id( 1 );
            scheme.add_raw_can_frames_to_collect();
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 3 );
            signalInformation->set_data_partition_id( 1 );
            scheme.add_raw_can_frames_to_collect();

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
            scheme.add_raw_can_frames_to_collect();
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 5 );
            signalInformation->set_data_partition_id( 0 );
            scheme.add_raw_can_frames_to_collect();
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 6 );
            signalInformation->set_data_partition_id( 1 );
            scheme.add_raw_can_frames_to_collect();
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 7 );
            signalInformation->set_data_partition_id( 1 );
            scheme.add_raw_can_frames_to_collect();

            campaign2 = scheme;
        }

        convertSchemes( { campaign1, campaign2 }, out );
        return out;
    }

    inline std::vector<CollectedCanRawFrame>
    fakeCanFrames()
    {
        std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf = {};
        CollectedCanRawFrame frame( 0, 0, mClock->systemTimeSinceEpochMs(), buf, 12 );
        return { frame };
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
            ASSERT_EQ( mStreamManager->appendToStreams( signal.data ), Store::StreamManager::ReturnCode::SUCCESS );
        }
    }

    void
    forwardAll( std::vector<CollectedSignals> collectedSignals )
    {
        for ( auto signals : collectedSignals )
        {
            for ( auto entry : signals.signals )
            {
                mStreamForwarder->beginForward(
                    signals.campaignID, entry.first, Aws::IoTFleetWise::Store::StreamForwarder::Source::CONDITION );
            }
        }
    }

    void
    forwardAllIoTJob( std::vector<CollectedSignals> collectedSignals, uint64_t endTime )
    {
        for ( auto signals : collectedSignals )
        {
            mStreamForwarder->beginJobForward( signals.campaignID, endTime );
        }
    }

    std::vector<CollectedSignals>
    buildTestData( const std::shared_ptr<Aws::IoTFleetWise::ActiveCollectionSchemes> &campaign,
                   Timestamp triggerTime = 0 )
    {
        std::vector<CollectedSignals> collectedSignals;
        for ( auto scheme : campaign->activeCollectionSchemes )
        {
            CollectedSignals signals;
            signals.campaignID = scheme->getCampaignArn();

            signals.data.eventID = 1234;
            signals.data.metadata.collectionSchemeID = scheme->getCollectionSchemeID();
            signals.data.metadata.campaignArn = signals.campaignID;
            signals.data.canFrames = fakeCanFrames();
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
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    storeAll( collectedSignals );
    forwardAll( collectedSignals );

    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 4 );
}

TEST_F( StreamForwarderTest, ForwardStopsForPartitionWhenRequested )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );
    forwardAll( collectedSignals );
    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 4 );
    messagesSent = 0;

    // stop uploading from one of the campaigns/partitions
    mStreamForwarder->cancelForward( campaign->activeCollectionSchemes[0]->getCampaignArn(),
                                     0,
                                     Aws::IoTFleetWise::Store::StreamForwarder::Source::CONDITION );
    storeAll( collectedSignals );
    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 3 );
}

TEST_F( StreamForwarderTest, ForwardStopsForCampaignsThatAreRemoved )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    storeAll( collectedSignals );

    // remove the campaign entirely
    mStreamManager->onChangeCollectionSchemeList( {} );

    forwardAll( collectedSignals );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) ); // give the thread time to attempt a forward

    ASSERT_EQ( static_cast<int>( messagesSent ), 0 );
}

TEST_F( StreamForwarderTest, StoreAndForwardDataFromMultipleCampaignsAndPartitionsIoTJobs )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    storeAll( collectedSignals );
    forwardAllIoTJob( collectedSignals, 0 );

    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 4 );
}

TEST_F( StreamForwarderTest, ForwardStopsForIoTJobWhenEndOfStream )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );
    forwardAllIoTJob( collectedSignals, 0 );
    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 4 );
    messagesSent = 0;

    // End of Stream is hit so forwarding has stopped
    storeAll( collectedSignals );
    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 0 );
}

TEST_F( StreamForwarderTest, ForwardWhenBothIotJobAndConditionAreActive )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    // store and forward all partitions
    storeAll( collectedSignals );
    forwardAllIoTJob( collectedSignals, 0 );
    forwardAll( collectedSignals );
    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 4 );
    messagesSent = 0;
    std::this_thread::sleep_for(
        std::chrono::milliseconds( 1000 ) ); // give thread time to update mJobCampaignToPartitions

    // End of Stream is hit so forwarding has stopped for IOT_JOB
    // stop uploading from one of the campaigns/partitions
    mStreamForwarder->cancelForward( campaign->activeCollectionSchemes[0]->getCampaignArn(),
                                     0,
                                     Aws::IoTFleetWise::Store::StreamForwarder::Source::CONDITION );
    storeAll( collectedSignals );
    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 3 );
}

TEST_F( StreamForwarderTest, ForwarderStopsForIoTJobWhenEndTime )
{
    // apply campaign config
    auto campaign =
        std::make_shared<Aws::IoTFleetWise::ActiveCollectionSchemes>( GetMultipleCampaignsWithMultiplePartitions() );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto collectedSignals = buildTestData( campaign );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    // isolate the endTime
    auto endTime = mClock->systemTimeSinceEpochMs();

    // build same test data but with collection triggertime after the endTime
    auto collectedSignals2 = buildTestData( campaign, endTime + 1 );

    auto combinedSignals = collectedSignals;
    combinedSignals.insert( combinedSignals.end(), collectedSignals2.begin(), collectedSignals2.end() );

    std::atomic<int> messagesSent{ 0 };
    trackNumMqttMessagesSent( messagesSent );

    // start the forwarder thread
    ASSERT_TRUE( mStreamForwarder->start() );
    ASSERT_TRUE( mStreamForwarder->isAlive() );

    storeAll( combinedSignals );
    // endTime will be after the collectedSignals collection trigger time, but before the collectedSignals2 collection
    // trigger time
    forwardAllIoTJob( combinedSignals, endTime );

    WAIT_ASSERT_EQ( static_cast<int>( messagesSent ), 4 );
}
} // namespace IoTFleetWise
} // namespace Aws
