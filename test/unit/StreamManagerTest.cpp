// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StreamManager.h"
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
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include "StoreFileSystem.h"
#include "StoreLogger.h"
#include "Testing.h"
#include "collection_schemes.pb.h"
#include <array>
#include <aws/store/common/expected.hpp>
#include <aws/store/filesystem/filesystem.hpp>
#include <aws/store/kv/kv.hpp>
#include <aws/store/stream/fileStream.hpp>
#include <aws/store/stream/stream.hpp>
#include <boost/filesystem.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <snappy.h>
#include <string>
#include <thread>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class StreamManagerTest : public ::testing::Test
{
protected:
    uint64_t partitionMaximumSizeInBytes = 1000000;

    StreamManagerTest()
    {
        mTranslator.add( "can123" );
        mPersistenceRootDir = getTempDir();
        auto protoWriter = std::make_shared<DataSenderProtoWriter>( mTranslator, nullptr );
        auto protoReader = std::make_shared<DataSenderProtoReader>( mTranslator );
        mStreamManager = std::make_shared<Store::StreamManager>( mPersistenceRootDir.string(), protoWriter, 0 );
        mProtoReader = std::make_shared<DataSenderProtoReader>( mTranslator );
        setupCampaignTestData();
    }

    CANInterfaceIDTranslator mTranslator;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<Store::Logger> mLogger = std::make_shared<Aws::IoTFleetWise::Store::Logger>();
    boost::filesystem::path mPersistenceRootDir;
    std::shared_ptr<Store::StreamManager> mStreamManager;
    std::shared_ptr<DataSenderProtoReader> mProtoReader;

    // campaign test data
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> noCampaigns;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> campaignWithSinglePartition;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> campaignWithTwoPartitions;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> campaignWithInvalidStorageLocation;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> campaignWithOneSecondTTL;

    void
    TearDown() override
    {
        mStreamManager->onChangeCollectionSchemeList( noCampaigns );
        ASSERT_TRUE( boost::filesystem::is_empty( mPersistenceRootDir ) );
        boost::filesystem::remove( mPersistenceRootDir );
    }

    void
    setupCampaignTestData()
    {
        // no campaigns
        {
            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertSchemes( {}, out );
            noCampaigns = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
        }
        // campaign with one partition
        {
            Schemas::CollectionSchemesMsg::CollectionScheme scheme;

            scheme.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_campaign_arn( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_decoder_manifest_sync_id( "model_manifest_13" );
            scheme.set_compress_collected_data( true );

            auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

            // partition 0
            auto *partition = store_and_forward_configuration->add_partition_configuration();
            auto *storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( partitionMaximumSizeInBytes );
            storageOptions->set_storage_location( "partition0" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

            // map signals to partitions
            auto *signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 0 );
            signalInformation->set_data_partition_id( 0 );
            scheme.add_raw_can_frames_to_collect();

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            campaignWithSinglePartition = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
        }
        // campaign with two partitions
        {
            Schemas::CollectionSchemesMsg::CollectionScheme scheme;
            scheme.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_campaign_arn( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_decoder_manifest_sync_id( "model_manifest_13" );

            auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

            auto *partition = store_and_forward_configuration->add_partition_configuration();
            // partition 0
            auto *storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( partitionMaximumSizeInBytes );
            storageOptions->set_storage_location( "partition0" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );
            // partition 1
            partition = store_and_forward_configuration->add_partition_configuration();
            storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( partitionMaximumSizeInBytes );
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

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            campaignWithTwoPartitions = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
        }
        // campaign with invalid storage location
        {
            Schemas::CollectionSchemesMsg::CollectionScheme scheme;

            scheme.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_campaign_arn( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_decoder_manifest_sync_id( "model_manifest_13" );

            auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

            // partition 0
            auto *partition = store_and_forward_configuration->add_partition_configuration();
            auto *storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( partitionMaximumSizeInBytes );
            storageOptions->set_storage_location( "../" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

            // map signals to partitions
            auto *signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 0 );
            signalInformation->set_data_partition_id( 0 );
            scheme.add_raw_can_frames_to_collect();

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            campaignWithInvalidStorageLocation =
                std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
        }
        // campaign with no ttl
        {
            Schemas::CollectionSchemesMsg::CollectionScheme scheme;

            scheme.set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_campaign_arn( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
            scheme.set_decoder_manifest_sync_id( "model_manifest_13" );

            auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

            // partition 0
            auto *partition = store_and_forward_configuration->add_partition_configuration();
            auto *storageOptions = partition->mutable_storage_options();
            storageOptions->set_maximum_size_in_bytes( partitionMaximumSizeInBytes );
            storageOptions->set_storage_location( "partition0" );
            storageOptions->set_minimum_time_to_live_in_seconds( 1 );

            // map signals to partitions
            auto *signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 0 );
            signalInformation->set_data_partition_id( 0 );
            scheme.add_raw_can_frames_to_collect();

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            campaignWithOneSecondTTL = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
        }
    }

    static void
    convertScheme( Schemas::CollectionSchemesMsg::CollectionScheme scheme,
                   Aws::IoTFleetWise::ActiveCollectionSchemes &convertedSchemes )
    {
        convertSchemes( std::vector<Schemas::CollectionSchemesMsg::CollectionScheme>{ scheme }, convertedSchemes );
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

    static inline std::string
    getCampaignID( std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> campaign )
    {
        return campaign->activeCollectionSchemes[0]->getCollectionSchemeID();
    }

    static inline std::string
    getCampaignArn( std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> campaign )
    {
        return campaign->activeCollectionSchemes[0]->getCampaignArn();
    }

    void
    verifyStreamLocationAndSize( boost::filesystem::path location, uint64_t size )
    {
        std::shared_ptr<aws::store::stream::FileStream> stream;
        getStream( location, stream );
        for ( uint64_t i = 0; i < size; ++i )
        {
            auto val_or = stream->read( i, aws::store::stream::ReadOptions{ {}, false, {} } );
            ASSERT_TRUE( val_or.ok() );
        }
        auto val_or = stream->read( size, aws::store::stream::ReadOptions{ {}, false, {} } );
        ASSERT_FALSE( val_or.ok() );
    }

    void
    getStream( boost::filesystem::path streamLocation, std::shared_ptr<aws::store::stream::FileStream> &out )
    {
        const std::shared_ptr<aws::store::filesystem::FileSystemInterface> fs =
            std::make_shared<Aws::IoTFleetWise::Store::PosixFileSystem>( streamLocation );
        auto stream_or = aws::store::stream::FileStream::openOrCreate( aws::store::stream::StreamOptions{
            mStreamManager->STREAM_DEFAULT_MIN_SEGMENT_SIZE,
            static_cast<uint32_t>( partitionMaximumSizeInBytes ),
            true,
            fs,
            mLogger,
            aws::store::kv::KVOptions{
                true,
                fs,
                mLogger,
                mStreamManager->KV_STORE_IDENTIFIER,
                mStreamManager->KV_COMPACT_AFTER,
            },
        } );
        ASSERT_TRUE( stream_or.ok() );
        out = stream_or.val();
    }

    int
    getNumStoreFiles()
    {
        auto numFiles = 0;
        for ( boost::filesystem::recursive_directory_iterator it( mPersistenceRootDir );
              it != boost::filesystem::recursive_directory_iterator();
              ++it )
        {
            if ( boost::filesystem::is_regular_file( *it ) )
            {
                ++numFiles;
            }
        }
        return numFiles;
    }

    void
    deserialize( std::string record, bool decompress, TriggeredCollectionSchemeData &data )
    {
        if ( decompress )
        {
            std::string out;
            ASSERT_TRUE( snappy::Uncompress( record.data(), record.size(), &out ) );
            record = out;
        }
        ASSERT_TRUE( mProtoReader->setupVehicleData( record ) );
        ASSERT_TRUE( mProtoReader->deserializeVehicleData( data ) );
    }
};

TEST_F( StreamManagerTest, StreamAppendFailsIfEmptyData )
{
    TriggeredCollectionSchemeData data{};
    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::STREAM_NOT_FOUND );
}

TEST_F( StreamManagerTest, StreamHasCamapaign )
{
    auto campaign = campaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    std::string unknownCampaignID = "unknownCampaignID";

    mStreamManager->onChangeCollectionSchemeList( campaign );

    ASSERT_TRUE( mStreamManager->hasCampaign( campaignID ) );
    ASSERT_FALSE( mStreamManager->hasCampaign( unknownCampaignID ) );
}

TEST_F( StreamManagerTest, StreamAppendsNoSignals )
{
    auto campaign = campaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignID;
    data.metadata.persist = true;
    data.canFrames = {};
    data.signals = {};

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::EMPTY_DATA );
}

TEST_F( StreamManagerTest, StreamAppendFailsWhenStorageLocationIsInvalid )
{
    auto campaign = campaignWithInvalidStorageLocation;
    auto campaignID = getCampaignID( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignID;
    data.metadata.persist = true;
    data.canFrames = {};
    data.signals = {};

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::STREAM_NOT_FOUND );
}

TEST_F( StreamManagerTest, StreamAppendFailsForNonExistantCampaign )
{
    auto campaign = campaignWithSinglePartition;
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.metadata.collectionSchemeID = "unknown";
    data.metadata.campaignArn = "unknown";
    data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 } };
    data.canFrames = fakeCanFrames();
    data.mDTCInfo = fakeDtcInfo();

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::STREAM_NOT_FOUND );
}

TEST_F( StreamManagerTest, StreamAppendOneSignalOnePartition )
{
    auto campaign = campaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.eventID = 1234;
    data.triggerTime = 12345567;
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignArn;
    data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                     CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 },
                     // signals that are not part of the campaign
                     CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 2, SignalType::UINT8 },
                     CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 3, SignalType::UINT8 },
                     CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 4, SignalType::UINT8 } };
    data.canFrames = fakeCanFrames();
    data.mDTCInfo = fakeDtcInfo();

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

    // ensure stream files are written to expected location
    auto expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

    // verify we can read back the data
    std::string record;
    Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
    std::function<void()> checkpoint;
    ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
               Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( metadata.triggerTime, data.triggerTime );
    checkpoint();

    TriggeredCollectionSchemeData readData;
    deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

    // verify data contents are correct
    ASSERT_EQ( readData.eventID, data.eventID );
    ASSERT_EQ( readData.triggerTime, data.triggerTime );
    // metadata
    ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
    ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
    ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
    ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
    ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
    // signals
    size_t expectedNumSignals = 2;
    ASSERT_EQ( readData.signals.size(), expectedNumSignals );
    for ( size_t i = 0; i < expectedNumSignals; ++i )
    {
        ASSERT_EQ( readData.signals[i].receiveTime, data.signals[i].receiveTime );
        ASSERT_EQ( readData.signals[i].signalID, data.signals[i].signalID );
        ASSERT_EQ( readData.signals[i].value.value.doubleVal,
                   static_cast<double>( data.signals[i].value.value.uint8Val ) );
    }
    // CAN frames
    ASSERT_EQ( readData.canFrames.size(), data.canFrames.size() );
    for ( size_t i = 0; i < data.canFrames.size(); ++i )
    {
        ASSERT_EQ( readData.canFrames[i].receiveTime, data.canFrames[i].receiveTime );
        ASSERT_EQ( readData.canFrames[i].channelId, data.canFrames[i].channelId );
        ASSERT_EQ( readData.canFrames[i].frameID, data.canFrames[i].frameID );
        ASSERT_EQ( readData.canFrames[i].size, data.canFrames[i].size );
        ASSERT_EQ( readData.canFrames[i].data, data.canFrames[i].data );
    }
    // DTC
    ASSERT_FALSE( readData.mDTCInfo.hasItems() );
}

TEST_F( StreamManagerTest, StreamAppendSignalsAcrossMultiplePartitions )
{
    auto campaign = campaignWithTwoPartitions;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.triggerTime = 12345567;
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignArn;
    // two signals for each of the two partitions
    data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                     CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 },
                     CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 2, SignalType::UINT8 },
                     CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 3, SignalType::UINT8 } };
    data.canFrames = fakeCanFrames();
    data.mDTCInfo = fakeDtcInfo();

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

    // ensure partition 0 is written to disk at the proper location and contains 1 entry
    auto expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

    // ensure partition 1 is written to disk at the proper location and contains 1 entry
    expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[1].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

    // verify we can read back partition 0 data
    {
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data.eventID );
        ASSERT_EQ( readData.triggerTime, data.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
        // signals
        ASSERT_EQ( readData.signals.size(), 2 );
        ASSERT_EQ( readData.signals[0].receiveTime, data.signals[0].receiveTime );
        ASSERT_EQ( readData.signals[0].signalID, data.signals[0].signalID );
        ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                   static_cast<double>( data.signals[0].value.value.uint8Val ) );
        ASSERT_EQ( readData.signals[1].receiveTime, data.signals[1].receiveTime );
        ASSERT_EQ( readData.signals[1].signalID, data.signals[1].signalID );
        ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                   static_cast<double>( data.signals[1].value.value.uint8Val ) );
        // CAN frames
        // all frames will be in partition 0 since it's considered the default
        ASSERT_EQ( readData.canFrames.size(), data.canFrames.size() );
        for ( size_t i = 0; i < data.canFrames.size(); ++i )
        {
            ASSERT_EQ( readData.canFrames[i].receiveTime, data.canFrames[i].receiveTime );
            ASSERT_EQ( readData.canFrames[i].channelId, data.canFrames[i].channelId );
            ASSERT_EQ( readData.canFrames[i].frameID, data.canFrames[i].frameID );
            ASSERT_EQ( readData.canFrames[i].size, data.canFrames[i].size );
            ASSERT_EQ( readData.canFrames[i].data, data.canFrames[i].data );
        }
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }
    {
        // verify we can read back partition 1 data
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data.eventID );
        ASSERT_EQ( readData.triggerTime, data.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
        // signals
        ASSERT_EQ( readData.signals.size(), 2 );
        ASSERT_EQ( readData.signals[0].receiveTime, data.signals[2].receiveTime );
        ASSERT_EQ( readData.signals[0].signalID, data.signals[2].signalID );
        ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                   static_cast<double>( data.signals[2].value.value.uint8Val ) );
        ASSERT_EQ( readData.signals[1].receiveTime, data.signals[3].receiveTime );
        ASSERT_EQ( readData.signals[1].signalID, data.signals[3].signalID );
        ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                   static_cast<double>( data.signals[3].value.value.uint8Val ) );
        // CAN frames
        // no frames since this is not the default partition
        ASSERT_TRUE( readData.canFrames.empty() );
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }
}

TEST_F( StreamManagerTest, StreamMultipleAppendSignalsAcrossMultiplePartitions )
{
    auto campaign = campaignWithTwoPartitions;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.triggerTime = 12345567;
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignArn;
    // two signals for each of the two partitions
    data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                     CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 },
                     CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 2, SignalType::UINT8 },
                     CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 3, SignalType::UINT8 } };
    data.canFrames = fakeCanFrames();
    data.mDTCInfo = fakeDtcInfo();
    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

    TriggeredCollectionSchemeData data2{};
    data2.triggerTime = 12345567;
    data2.metadata.collectionSchemeID = campaignID;
    data2.metadata.campaignArn = campaignID;
    // two signals for each of the two partitions
    data2.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 4, SignalType::UINT8 },
                      CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 5, SignalType::UINT8 },
                      CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 6, SignalType::UINT8 },
                      CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 7, SignalType::UINT8 } };
    data2.canFrames = fakeCanFrames();
    data2.mDTCInfo = fakeDtcInfo();
    ASSERT_EQ( mStreamManager->appendToStreams( data2 ), Store::StreamManager::ReturnCode::SUCCESS );

    // ensure partition 0 is written to disk at the proper location and contains 2 entries
    auto expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 2 );

    // ensure partition 1 is written to disk at the proper location and contains 2 entries
    expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[1].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 2 );

    // verify we can read back partition 0 data

    {
        // partition 0 entry 1
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data.eventID );
        ASSERT_EQ( readData.triggerTime, data.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
        // signals
        ASSERT_EQ( readData.signals.size(), 2 );
        ASSERT_EQ( readData.signals[0].receiveTime, data.signals[0].receiveTime );
        ASSERT_EQ( readData.signals[0].signalID, data.signals[0].signalID );
        ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                   static_cast<double>( data.signals[0].value.value.uint8Val ) );
        ASSERT_EQ( readData.signals[1].receiveTime, data.signals[1].receiveTime );
        ASSERT_EQ( readData.signals[1].signalID, data.signals[1].signalID );
        ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                   static_cast<double>( data.signals[1].value.value.uint8Val ) );
        // CAN frames
        // all frames will be in partition 0 since it's considered the default
        ASSERT_EQ( readData.canFrames.size(), data.canFrames.size() );
        for ( size_t i = 0; i < data.canFrames.size(); ++i )
        {
            ASSERT_EQ( readData.canFrames[i].receiveTime, data.canFrames[i].receiveTime );
            ASSERT_EQ( readData.canFrames[i].channelId, data.canFrames[i].channelId );
            ASSERT_EQ( readData.canFrames[i].frameID, data.canFrames[i].frameID );
            ASSERT_EQ( readData.canFrames[i].size, data.canFrames[i].size );
            ASSERT_EQ( readData.canFrames[i].data, data.canFrames[i].data );
        }
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }

    {
        // partition 0 entry 2
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data2.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data2.eventID );
        ASSERT_EQ( readData.triggerTime, data2.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data2.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data2.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data2.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data2.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data2.metadata.priority );
        // signals
        ASSERT_EQ( readData.signals.size(), 2 );
        ASSERT_EQ( readData.signals[0].receiveTime, data2.signals[0].receiveTime );
        ASSERT_EQ( readData.signals[0].signalID, data2.signals[0].signalID );
        ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                   static_cast<double>( data2.signals[0].value.value.uint8Val ) );
        ASSERT_EQ( readData.signals[1].receiveTime, data2.signals[1].receiveTime );
        ASSERT_EQ( readData.signals[1].signalID, data2.signals[1].signalID );
        ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                   static_cast<double>( data2.signals[1].value.value.uint8Val ) );
        // CAN frames
        // all frames will be in partition 0 since it's considered the default
        ASSERT_EQ( readData.canFrames.size(), data2.canFrames.size() );
        for ( size_t i = 0; i < data2.canFrames.size(); ++i )
        {
            ASSERT_EQ( readData.canFrames[i].receiveTime, data2.canFrames[i].receiveTime );
            ASSERT_EQ( readData.canFrames[i].channelId, data2.canFrames[i].channelId );
            ASSERT_EQ( readData.canFrames[i].frameID, data2.canFrames[i].frameID );
            ASSERT_EQ( readData.canFrames[i].size, data2.canFrames[i].size );
            ASSERT_EQ( readData.canFrames[i].data, data2.canFrames[i].data );
        }
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }

    // verify we can read back partition 1 data

    {
        // partition 1 entry 1
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data.eventID );
        ASSERT_EQ( readData.triggerTime, data.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
        // signals
        ASSERT_EQ( readData.signals.size(), 2 );
        ASSERT_EQ( readData.signals[0].receiveTime, data.signals[2].receiveTime );
        ASSERT_EQ( readData.signals[0].signalID, data.signals[2].signalID );
        ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                   static_cast<double>( data.signals[2].value.value.uint8Val ) );
        ASSERT_EQ( readData.signals[1].receiveTime, data.signals[3].receiveTime );
        ASSERT_EQ( readData.signals[1].signalID, data.signals[3].signalID );
        ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                   static_cast<double>( data.signals[3].value.value.uint8Val ) );
        // CAN frames
        // no frames since this is not the default partition
        ASSERT_TRUE( readData.canFrames.empty() );
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }

    {
        // partition 1 entry 2
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data2.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data2.eventID );
        ASSERT_EQ( readData.triggerTime, data2.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data2.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data2.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data2.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data2.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data2.metadata.priority );
        // signals
        ASSERT_EQ( readData.signals.size(), 2 );
        ASSERT_EQ( readData.signals[0].receiveTime, data2.signals[2].receiveTime );
        ASSERT_EQ( readData.signals[0].signalID, data2.signals[2].signalID );
        ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                   static_cast<double>( data2.signals[2].value.value.uint8Val ) );
        ASSERT_EQ( readData.signals[1].receiveTime, data2.signals[3].receiveTime );
        ASSERT_EQ( readData.signals[1].signalID, data2.signals[3].signalID );
        ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                   static_cast<double>( data2.signals[3].value.value.uint8Val ) );
        // CAN frames
        // no frames since this is not the default partition
        ASSERT_TRUE( readData.canFrames.empty() );
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }
}

TEST_F( StreamManagerTest, StreamConfigChangeWithSameName )
{
    // test prereq: campaigns must have same name
    ASSERT_EQ( getCampaignID( campaignWithSinglePartition ), getCampaignID( campaignWithTwoPartitions ) );

    // verify we can set campaign config and append to a stream
    {
        auto campaign = campaignWithSinglePartition;
        auto campaignID = getCampaignID( campaign );
        auto campaignArn = getCampaignArn( campaign );
        mStreamManager->onChangeCollectionSchemeList( campaign );

        TriggeredCollectionSchemeData data{};
        data.eventID = 1234;
        data.triggerTime = 12345567;
        data.metadata.collectionSchemeID = campaignID;
        data.metadata.campaignArn = campaignArn;
        data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                         CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 },
                         // signals that are not part of the campaign
                         CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 2, SignalType::UINT8 },
                         CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 3, SignalType::UINT8 },
                         CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 4, SignalType::UINT8 } };
        data.canFrames = fakeCanFrames();
        data.mDTCInfo = fakeDtcInfo();

        ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

        // ensure stream files are written to expected location
        auto expectedPartitionLocation =
            mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
            campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
        verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

        // verify we can read back the data
        std::string record;
        Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, data.triggerTime );
        checkpoint();

        TriggeredCollectionSchemeData readData;
        deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

        // verify data contents are correct
        ASSERT_EQ( readData.eventID, data.eventID );
        ASSERT_EQ( readData.triggerTime, data.triggerTime );
        // metadata
        ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
        ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
        ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
        ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
        ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
        // signals
        size_t expectedNumSignals = 2;
        ASSERT_EQ( readData.signals.size(), expectedNumSignals );
        for ( size_t i = 0; i < expectedNumSignals; ++i )
        {
            ASSERT_EQ( readData.signals[i].receiveTime, data.signals[i].receiveTime );
            ASSERT_EQ( readData.signals[i].signalID, data.signals[i].signalID );
            ASSERT_EQ( readData.signals[i].value.value.doubleVal,
                       static_cast<double>( data.signals[i].value.value.uint8Val ) );
        }
        // CAN frames
        ASSERT_EQ( readData.canFrames.size(), data.canFrames.size() );
        for ( size_t i = 0; i < data.canFrames.size(); ++i )
        {
            ASSERT_EQ( readData.canFrames[i].receiveTime, data.canFrames[i].receiveTime );
            ASSERT_EQ( readData.canFrames[i].channelId, data.canFrames[i].channelId );
            ASSERT_EQ( readData.canFrames[i].frameID, data.canFrames[i].frameID );
            ASSERT_EQ( readData.canFrames[i].size, data.canFrames[i].size );
            ASSERT_EQ( readData.canFrames[i].data, data.canFrames[i].data );
        }
        // DTC
        ASSERT_FALSE( readData.mDTCInfo.hasItems() );
    }
    // verify we can set a new campaign config, with the same name as the previous, and the new config will take effect
    {
        auto campaign = campaignWithTwoPartitions;
        auto campaignID = getCampaignID( campaign );
        auto campaignArn = getCampaignArn( campaign );
        mStreamManager->onChangeCollectionSchemeList( campaign );

        TriggeredCollectionSchemeData data{};
        data.triggerTime = 12345567;
        data.metadata.collectionSchemeID = campaignID;
        data.metadata.campaignArn = campaignArn;
        // two signals for each of the two partitions
        data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                         CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 },
                         CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 2, SignalType::UINT8 },
                         CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 3, SignalType::UINT8 } };
        data.canFrames = fakeCanFrames();
        data.mDTCInfo = fakeDtcInfo();

        ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

        // ensure partition 0 is written to disk at the proper location and contains 1 entry
        auto expectedPartitionLocation =
            mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
            campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
        verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

        // ensure partition 1 is written to disk at the proper location and contains 1 entry
        expectedPartitionLocation =
            mPersistenceRootDir / boost::filesystem::path{ mStreamManager->getName( campaignID ) } /
            campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[1].storageOptions.storageLocation;
        verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

        // verify we can read back partition 0 data
        {
            std::string record;
            Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
            std::function<void()> checkpoint;
            ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                       Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
            ASSERT_EQ( metadata.triggerTime, data.triggerTime );
            checkpoint();

            TriggeredCollectionSchemeData readData;
            deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

            // verify data contents are correct
            ASSERT_EQ( readData.eventID, data.eventID );
            ASSERT_EQ( readData.triggerTime, data.triggerTime );
            // metadata
            ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
            ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
            ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
            ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
            ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
            // signals
            ASSERT_EQ( readData.signals.size(), 2 );
            ASSERT_EQ( readData.signals[0].receiveTime, data.signals[0].receiveTime );
            ASSERT_EQ( readData.signals[0].signalID, data.signals[0].signalID );
            ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                       static_cast<double>( data.signals[0].value.value.uint8Val ) );
            ASSERT_EQ( readData.signals[1].receiveTime, data.signals[1].receiveTime );
            ASSERT_EQ( readData.signals[1].signalID, data.signals[1].signalID );
            ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                       static_cast<double>( data.signals[1].value.value.uint8Val ) );
            // CAN frames
            // all frames will be in partition 0 since it's considered the default
            ASSERT_EQ( readData.canFrames.size(), data.canFrames.size() );
            for ( size_t i = 0; i < data.canFrames.size(); ++i )
            {
                ASSERT_EQ( readData.canFrames[i].receiveTime, data.canFrames[i].receiveTime );
                ASSERT_EQ( readData.canFrames[i].channelId, data.canFrames[i].channelId );
                ASSERT_EQ( readData.canFrames[i].frameID, data.canFrames[i].frameID );
                ASSERT_EQ( readData.canFrames[i].size, data.canFrames[i].size );
                ASSERT_EQ( readData.canFrames[i].data, data.canFrames[i].data );
            }
            // DTC
            ASSERT_FALSE( readData.mDTCInfo.hasItems() );
        }
        {
            // verify we can read back partition 1 data
            std::string record;
            Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
            std::function<void()> checkpoint;
            ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                       Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
            ASSERT_EQ( metadata.triggerTime, data.triggerTime );
            checkpoint();

            TriggeredCollectionSchemeData readData;
            deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

            // verify data contents are correct
            ASSERT_EQ( readData.eventID, data.eventID );
            ASSERT_EQ( readData.triggerTime, data.triggerTime );
            // metadata
            ASSERT_EQ( readData.metadata.collectionSchemeID, data.metadata.collectionSchemeID );
            ASSERT_EQ( readData.metadata.decoderID, data.metadata.decoderID );
            ASSERT_EQ( readData.metadata.persist, data.metadata.persist );
            ASSERT_EQ( readData.metadata.compress, data.metadata.compress );
            ASSERT_EQ( readData.metadata.priority, data.metadata.priority );
            // signals
            ASSERT_EQ( readData.signals.size(), 2 );
            ASSERT_EQ( readData.signals[0].receiveTime, data.signals[2].receiveTime );
            ASSERT_EQ( readData.signals[0].signalID, data.signals[2].signalID );
            ASSERT_EQ( readData.signals[0].value.value.doubleVal,
                       static_cast<double>( data.signals[2].value.value.uint8Val ) );
            ASSERT_EQ( readData.signals[1].receiveTime, data.signals[3].receiveTime );
            ASSERT_EQ( readData.signals[1].signalID, data.signals[3].signalID );
            ASSERT_EQ( readData.signals[1].value.value.doubleVal,
                       static_cast<double>( data.signals[3].value.value.uint8Val ) );
            // CAN frames
            // no frames since this is not the default partition
            ASSERT_TRUE( readData.canFrames.empty() );
            // DTC
            ASSERT_FALSE( readData.mDTCInfo.hasItems() );
        }
    }
}

TEST_F( StreamManagerTest, ExtraStreamFilesAreDeleted )
{
    ASSERT_TRUE( boost::filesystem::is_empty( mPersistenceRootDir ) );

    // add one campaign/partition to stream manager
    auto campaign = campaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign ); // creates the kv store file

    TriggeredCollectionSchemeData data{};
    data.eventID = 1234;
    data.triggerTime = 12345567;
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignArn;
    data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                     CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 } };
    data.canFrames = fakeCanFrames();
    data.mDTCInfo = fakeDtcInfo();
    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS ); // creates log file

    ASSERT_EQ( getNumStoreFiles(), 2 ); // stream log, kv store

    // create files for a stream that's unknown to stream manager
    std::set<boost::filesystem::path> paths = { mPersistenceRootDir / boost::filesystem::path{ "fake-campaign" } /
                                                    boost::filesystem::path{ "fake-storage-location" } /
                                                    boost::filesystem::path{ "0.log" },
                                                mPersistenceRootDir / boost::filesystem::path{ "fake-campaign" } /
                                                    boost::filesystem::path{ "fake-storage-location" } /
                                                    boost::filesystem::path{ mStreamManager->KV_STORE_IDENTIFIER } };
    for ( auto path : paths )
    {
        boost::filesystem::create_directories( path.parent_path() );
        std::ofstream f( path.c_str() );
        f << "Hello World";
    }
    ASSERT_EQ( getNumStoreFiles(), 2 + paths.size() );

    // next time there's a collection scheme change, stream manager performs cleanup
    mStreamManager->onChangeCollectionSchemeList( campaign );
    ASSERT_EQ( getNumStoreFiles(), 2 );
}

TEST_F( StreamManagerTest, StreamExpiresOldRecordsOnCollectionSchemeChange )
{
    auto campaign = campaignWithOneSecondTTL;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    TriggeredCollectionSchemeData data{};
    data.triggerTime = 12345567;
    data.metadata.collectionSchemeID = campaignID;
    data.metadata.campaignArn = campaignArn;
    // two signals for each of the two partitions
    data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT8 },
                     CollectedSignal{ 1, mClock->systemTimeSinceEpochMs(), 1, SignalType::UINT8 },
                     CollectedSignal{ 2, mClock->systemTimeSinceEpochMs(), 2, SignalType::UINT8 },
                     CollectedSignal{ 3, mClock->systemTimeSinceEpochMs(), 3, SignalType::UINT8 } };
    data.canFrames = fakeCanFrames();
    data.mDTCInfo = fakeDtcInfo();

    // add a record to the stream
    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );
    std::string record;
    Aws::IoTFleetWise::Store::StreamManager::RecordMetadata metadata;
    std::function<void()> checkpoint;
    ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
               Aws::IoTFleetWise::Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( metadata.triggerTime, data.triggerTime );

    checkpoint();

    TriggeredCollectionSchemeData readData;
    deserialize( record, campaign->activeCollectionSchemes[0]->isCompressionNeeded(), readData );

    // expire records
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    // verify records were removed
    ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
               Aws::IoTFleetWise::Store::StreamManager::ReturnCode::END_OF_STREAM );
}

} // namespace IoTFleetWise
} // namespace Aws
