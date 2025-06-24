// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/StreamManager.h"
#include "Testing.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionSchemeIngestion.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/snf/DataSenderProtoReader.h"
#include "aws/iotfleetwise/snf/StoreFileSystem.h"
#include "aws/iotfleetwise/snf/StoreLogger.h"
#include "collection_schemes.pb.h"
#include <aws/store/common/expected.hpp>
#include <aws/store/filesystem/filesystem.hpp>
#include <aws/store/kv/kv.hpp>
#include <aws/store/stream/fileStream.hpp>
#include <aws/store/stream/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/optional/optional.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <set>
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
        auto protoReader = std::make_shared<DataSenderProtoReader>( mTranslator );
        mStreamManager = std::make_shared<Store::StreamManager>( mPersistenceRootDir.string() );
        mProtoReader = std::make_shared<DataSenderProtoReader>( mTranslator );
        setupCampaignTestData();
    }

    CANInterfaceIDTranslator mTranslator;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<Store::Logger> mLogger = std::make_shared<Store::Logger>();
    boost::filesystem::path mPersistenceRootDir;
    std::shared_ptr<Store::StreamManager> mStreamManager;
    std::shared_ptr<DataSenderProtoReader> mProtoReader;

    // campaign test data
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> mNoCampaigns;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> mCampaignWithSinglePartition;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> mCampaignWithTwoPartitions;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> mCampaignWithInvalidStorageLocation;
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> mCampaignWithOneSecondTTL;

    void
    TearDown() override
    {
        mStreamManager->onChangeCollectionSchemeList( mNoCampaigns );
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
            mNoCampaigns = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
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

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            mCampaignWithSinglePartition = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
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
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 1 );
            signalInformation->set_data_partition_id( 0 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 2 );
            signalInformation->set_data_partition_id( 1 );
            signalInformation = scheme.add_signal_information();
            signalInformation->set_signal_id( 3 );
            signalInformation->set_data_partition_id( 1 );

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            mCampaignWithTwoPartitions = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
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

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            mCampaignWithInvalidStorageLocation =
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

            Aws::IoTFleetWise::ActiveCollectionSchemes out;
            convertScheme( scheme, out );
            mCampaignWithOneSecondTTL = std::make_shared<const Aws::IoTFleetWise::ActiveCollectionSchemes>( out );
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
            std::make_shared<Store::PosixFileSystem>( streamLocation );
        auto stream_or = aws::store::stream::FileStream::openOrCreate( aws::store::stream::StreamOptions{
            Store::StreamManager::STREAM_DEFAULT_MIN_SEGMENT_SIZE,
            static_cast<uint32_t>( partitionMaximumSizeInBytes ),
            true,
            fs,
            mLogger,
            aws::store::kv::KVOptions{
                true,
                fs,
                mLogger,
                Store::StreamManager::KV_STORE_IDENTIFIER,
                Store::StreamManager::KV_COMPACT_AFTER,
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
};

TEST_F( StreamManagerTest, StreamAppendFailsIfEmptyData )
{
    TelemetryDataToPersist data( CollectionSchemeParams(), 1, std::make_shared<std::string>(), 1, 0 );
    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::STREAM_NOT_FOUND );
}

TEST_F( StreamManagerTest, StreamHasCampaign )
{
    auto campaign = mCampaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    std::string unknownCampaignID = "unknownCampaignID";

    mStreamManager->onChangeCollectionSchemeList( campaign );

    ASSERT_TRUE( mStreamManager->hasCampaign( campaignID ) );
    ASSERT_FALSE( mStreamManager->hasCampaign( unknownCampaignID ) );
}

TEST_F( StreamManagerTest, StreamAppendFailsWhenStorageLocationIsInvalid )
{
    auto campaign = mCampaignWithInvalidStorageLocation;
    auto campaignID = getCampaignID( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.collectionSchemeID = campaignID;
    collectionSchemeParams.campaignArn = campaignID;
    collectionSchemeParams.persist = true;
    TelemetryDataToPersist data( collectionSchemeParams, 1, std::make_shared<std::string>(), 1, 0 );

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::STREAM_NOT_FOUND );
}

TEST_F( StreamManagerTest, StreamAppendFailsForNonExistantCampaign )
{
    auto campaign = mCampaignWithSinglePartition;
    mStreamManager->onChangeCollectionSchemeList( campaign );

    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.collectionSchemeID = "unknown";
    collectionSchemeParams.campaignArn = "unknown";
    TelemetryDataToPersist data( collectionSchemeParams, 1, std::make_shared<std::string>(), 1, 0 );

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::STREAM_NOT_FOUND );
}

TEST_F( StreamManagerTest, StreamAppendOneSignalOnePartition )
{
    auto campaign = mCampaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto serializedData = std::make_shared<std::string>( "fake raw data" );
    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.triggerTime = 1234567;
    collectionSchemeParams.collectionSchemeID = campaignID;
    collectionSchemeParams.campaignArn = campaignArn;
    TelemetryDataToPersist data( collectionSchemeParams, 1, serializedData, 0, 0 );

    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

    // ensure stream files are written to expected location
    auto expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

    // verify we can read back the data
    std::string record;
    Store::StreamManager::RecordMetadata metadata;
    std::function<void()> checkpoint;
    ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
               Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
    checkpoint();

    ASSERT_EQ( record, *serializedData );
}

TEST_F( StreamManagerTest, StreamAppendSignalsAcrossMultiplePartitions )
{
    auto campaign = mCampaignWithTwoPartitions;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto serializedData1 = std::make_shared<std::string>( "fake raw data partition 0" );
    auto serializedData2 = std::make_shared<std::string>( "fake raw data partition 1" );
    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.triggerTime = 1234567;
    collectionSchemeParams.collectionSchemeID = campaignID;
    collectionSchemeParams.campaignArn = campaignArn;
    TelemetryDataToPersist data1( collectionSchemeParams, 1, serializedData1, 0, 0 );
    TelemetryDataToPersist data2( collectionSchemeParams, 1, serializedData2, 1, 0 );

    ASSERT_EQ( mStreamManager->appendToStreams( data1 ), Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( mStreamManager->appendToStreams( data2 ), Store::StreamManager::ReturnCode::SUCCESS );

    // ensure partition 0 is written to disk at the proper location and contains 1 entry
    auto expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

    // ensure partition 1 is written to disk at the proper location and contains 1 entry
    expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[1].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

    // verify we can read back partition 0 data
    {
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData1 );
    }
    {
        // verify we can read back partition 1 data
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData2 );
    }
}

TEST_F( StreamManagerTest, StreamMultipleAppendSignalsAcrossMultiplePartitions )
{
    auto campaign = mCampaignWithTwoPartitions;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto serializedData1Partition0 = std::make_shared<std::string>( "fake raw data 1 partition 0" );
    auto serializedData2Partition0 = std::make_shared<std::string>( "fake raw data 2 partition 0" );
    auto serializedData1Partition1 = std::make_shared<std::string>( "fake raw data 1 partition 1" );
    auto serializedData2Partition1 = std::make_shared<std::string>( "fake raw data 2 partition 1" );
    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.triggerTime = 1234567;
    collectionSchemeParams.collectionSchemeID = campaignID;
    collectionSchemeParams.campaignArn = campaignArn;
    TelemetryDataToPersist data1Partition0( collectionSchemeParams, 1, serializedData1Partition0, 0, 0 );
    TelemetryDataToPersist data2Partition0( collectionSchemeParams, 1, serializedData2Partition0, 0, 0 );
    TelemetryDataToPersist data1Partition1( collectionSchemeParams, 1, serializedData1Partition1, 1, 0 );
    TelemetryDataToPersist data2Partition1( collectionSchemeParams, 1, serializedData2Partition1, 1, 0 );

    ASSERT_EQ( mStreamManager->appendToStreams( data1Partition0 ), Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( mStreamManager->appendToStreams( data1Partition1 ), Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( mStreamManager->appendToStreams( data2Partition0 ), Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( mStreamManager->appendToStreams( data2Partition1 ), Store::StreamManager::ReturnCode::SUCCESS );

    // ensure partition 0 is written to disk at the proper location and contains 2 entries
    auto expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 2 );

    // ensure partition 1 is written to disk at the proper location and contains 2 entries
    expectedPartitionLocation =
        mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
        campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[1].storageOptions.storageLocation;
    verifyStreamLocationAndSize( expectedPartitionLocation, 2 );

    // verify we can read back partition 0 data

    {
        // partition 0 entry 1
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData1Partition0 );
    }

    {
        // partition 0 entry 2
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData2Partition0 );
    }

    // verify we can read back partition 1 data

    {
        // partition 1 entry 1
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData1Partition1 );
    }

    {
        // partition 1 entry 2
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );

        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData2Partition1 );
    }
}

TEST_F( StreamManagerTest, StreamConfigChangeWithSameName )
{
    // test prereq: campaigns must have same name
    ASSERT_EQ( getCampaignID( mCampaignWithSinglePartition ), getCampaignID( mCampaignWithTwoPartitions ) );

    // verify we can set campaign config and append to a stream
    {
        auto campaign = mCampaignWithSinglePartition;
        auto campaignID = getCampaignID( campaign );
        auto campaignArn = getCampaignArn( campaign );
        mStreamManager->onChangeCollectionSchemeList( campaign );

        auto serializedData = std::make_shared<std::string>( "fake raw data" );
        CollectionSchemeParams collectionSchemeParams;
        collectionSchemeParams.triggerTime = 1234567;
        collectionSchemeParams.collectionSchemeID = campaignID;
        collectionSchemeParams.campaignArn = campaignArn;
        TelemetryDataToPersist data( collectionSchemeParams, 1, serializedData, 0, 0 );

        ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );

        // ensure stream files are written to expected location
        auto expectedPartitionLocation =
            mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
            campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
        verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

        // verify we can read back the data
        std::string record;
        Store::StreamManager::RecordMetadata metadata;
        std::function<void()> checkpoint;
        ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                   Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
        checkpoint();

        ASSERT_EQ( record, *serializedData );
    }
    // verify we can set a new campaign config, with the same name as the previous, and the new config will take effect
    {
        auto campaign = mCampaignWithTwoPartitions;
        auto campaignID = getCampaignID( campaign );
        auto campaignArn = getCampaignArn( campaign );
        mStreamManager->onChangeCollectionSchemeList( campaign );

        auto serializedData1 = std::make_shared<std::string>( "fake raw data partition 0" );
        auto serializedData2 = std::make_shared<std::string>( "fake raw data partition 1" );
        CollectionSchemeParams collectionSchemeParams;
        collectionSchemeParams.triggerTime = 1234567;
        collectionSchemeParams.collectionSchemeID = campaignID;
        collectionSchemeParams.campaignArn = campaignArn;
        TelemetryDataToPersist data1( collectionSchemeParams, 1, serializedData1, 0, 0 );
        TelemetryDataToPersist data2( collectionSchemeParams, 1, serializedData2, 1, 0 );

        ASSERT_EQ( mStreamManager->appendToStreams( data1 ), Store::StreamManager::ReturnCode::SUCCESS );
        ASSERT_EQ( mStreamManager->appendToStreams( data2 ), Store::StreamManager::ReturnCode::SUCCESS );

        // ensure partition 0 is written to disk at the proper location and contains 1 entry
        auto expectedPartitionLocation =
            mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
            campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[0].storageOptions.storageLocation;
        verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

        // ensure partition 1 is written to disk at the proper location and contains 1 entry
        expectedPartitionLocation =
            mPersistenceRootDir / boost::filesystem::path{ Store::StreamManager::getName( campaignID ) } /
            campaign->activeCollectionSchemes[0]->getStoreAndForwardConfiguration()[1].storageOptions.storageLocation;
        verifyStreamLocationAndSize( expectedPartitionLocation, 1 );

        // verify we can read back partition 0 data
        {
            std::string record;
            Store::StreamManager::RecordMetadata metadata;
            std::function<void()> checkpoint;
            ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
                       Store::StreamManager::ReturnCode::SUCCESS );
            ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
            checkpoint();

            ASSERT_EQ( record, *serializedData1 );
        }
        {
            // verify we can read back partition 1 data
            std::string record;
            Store::StreamManager::RecordMetadata metadata;
            std::function<void()> checkpoint;
            ASSERT_EQ( mStreamManager->readFromStream( campaignID, 1, record, metadata, checkpoint ),
                       Store::StreamManager::ReturnCode::SUCCESS );

            ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );
            checkpoint();

            ASSERT_EQ( record, *serializedData2 );
        }
    }
}

TEST_F( StreamManagerTest, ExtraStreamFilesAreDeleted )
{
    ASSERT_TRUE( boost::filesystem::is_empty( mPersistenceRootDir ) );

    // add one campaign/partition to stream manager
    auto campaign = mCampaignWithSinglePartition;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign ); // creates the kv store file

    auto serializedData = std::make_shared<std::string>( "fake raw data" );
    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.triggerTime = 1234567;
    collectionSchemeParams.collectionSchemeID = campaignID;
    collectionSchemeParams.campaignArn = campaignArn;
    TelemetryDataToPersist data( collectionSchemeParams, 1, serializedData, 0, 0 );

    ASSERT_EQ( mStreamManager->appendToStreams( data ),
               Store::StreamManager::ReturnCode::SUCCESS ); // creates log file

    ASSERT_EQ( getNumStoreFiles(), 2 ); // stream log, kv store

    // create files for a stream that's unknown to stream manager
    std::set<boost::filesystem::path> paths = {
        mPersistenceRootDir / boost::filesystem::path{ "fake-campaign" } /
            boost::filesystem::path{ "fake-storage-location" } / boost::filesystem::path{ "0.log" },
        mPersistenceRootDir / boost::filesystem::path{ "fake-campaign" } /
            boost::filesystem::path{ "fake-storage-location" } /
            boost::filesystem::path{ Store::StreamManager::KV_STORE_IDENTIFIER } };
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
    auto campaign = mCampaignWithOneSecondTTL;
    auto campaignID = getCampaignID( campaign );
    auto campaignArn = getCampaignArn( campaign );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    auto serializedData = std::make_shared<std::string>( "fake raw data" );
    CollectionSchemeParams collectionSchemeParams;
    collectionSchemeParams.triggerTime = 1234567;
    collectionSchemeParams.collectionSchemeID = campaignID;
    collectionSchemeParams.campaignArn = campaignArn;
    TelemetryDataToPersist data( collectionSchemeParams, 1, serializedData, 0, 0 );

    // add a record to the stream
    ASSERT_EQ( mStreamManager->appendToStreams( data ), Store::StreamManager::ReturnCode::SUCCESS );
    std::string record;
    Store::StreamManager::RecordMetadata metadata;
    std::function<void()> checkpoint;
    ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
               Store::StreamManager::ReturnCode::SUCCESS );
    ASSERT_EQ( metadata.triggerTime, collectionSchemeParams.triggerTime );

    checkpoint();

    // expire records
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    mStreamManager->onChangeCollectionSchemeList( campaign );

    // verify records were removed
    ASSERT_EQ( mStreamManager->readFromStream( campaignID, 0, record, metadata, checkpoint ),
               Store::StreamManager::ReturnCode::END_OF_STREAM );
}

} // namespace IoTFleetWise
} // namespace Aws
