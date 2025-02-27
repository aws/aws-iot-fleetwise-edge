// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/StreamManager.h"
#include "aws/iotfleetwise/Assert.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/snf/StoreFileSystem.h"
#include "aws/iotfleetwise/snf/StoreLogger.h"

#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <algorithm>
#include <aws/store/common/expected.hpp>
#include <aws/store/common/slices.hpp>
#include <aws/store/common/util.hpp>
#include <aws/store/filesystem/filesystem.hpp>
#include <aws/store/kv/kv.hpp>
#include <aws/store/stream/fileStream.hpp>
#include <aws/store/stream/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/system/error_code.hpp>
#include <climits>
#include <cstring>
#include <mutex>
#include <set>
#include <snappy.h>
#include <unordered_set>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

const PartitionID StreamManager::DEFAULT_PARTITION = 0;
const std::string StreamManager::STREAM_ITER_IDENTIFIER = "i";
const std::string StreamManager::KV_STORE_IDENTIFIER = "s";
const int32_t StreamManager::KV_COMPACT_AFTER = 1024;
const uint32_t StreamManager::STREAM_DEFAULT_MIN_SEGMENT_SIZE = 16U * 1024U;

StreamManager::StreamManager( std::string persistenceRootDir,
                              std::unique_ptr<DataSenderProtoWriter> protoWriter,
                              uint32_t transmitThreshold )
    : mProtoWriter( std::move( protoWriter ) )
    , mTransmitThreshold{ ( transmitThreshold > 0U ) ? transmitThreshold : UINT_MAX }
    , mPersistenceRootDir( std::move( persistenceRootDir ) )
    , mLogger{ std::make_shared<Aws::IoTFleetWise::Store::Logger>() }
{
}

void
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
StreamManager::onChangeCollectionSchemeList(
    std::shared_ptr<const Aws::IoTFleetWise::ActiveCollectionSchemes> activeCollectionSchemes )
{
    // mark removed campaigns for deletion
    std::vector<std::pair<CampaignName, Campaign>> campaignsToErase;
    for ( const auto &campaign : mCampaigns )
    {
        // delete campaign if it's not present in new config
        if ( activeCollectionSchemes == nullptr )
        {
            campaignsToErase.emplace_back( campaign );
            continue;
        }
        std::shared_ptr<ICollectionScheme> newCampaignConfig;
        for ( const auto &conf : activeCollectionSchemes->activeCollectionSchemes )
        {
            if ( conf == nullptr )
            {
                continue;
            }
            if ( ( getName( conf->getCampaignArn() ) == campaign.first ) && ( campaign.second.config == conf ) )
            {
                newCampaignConfig = conf;
                break;
            }
        }
        if ( newCampaignConfig == nullptr )
        {
            campaignsToErase.emplace_back( campaign );
        }
    }

    // delete streams for removed campaigns
    for ( const auto &campaign : campaignsToErase )
    {
        {
            std::lock_guard<std::mutex> lock( mCampaignsMutex );

            // clear internal state
            mCampaigns.erase( campaign.first );
        }
        // remove all partitions for campaign from disk
        boost::filesystem::path campaignPath =
            boost::filesystem::path{ mPersistenceRootDir } / boost::filesystem::path{ campaign.first };
        boost::system::error_code ec;
        boost::filesystem::remove_all( campaignPath, ec );
        FWE_GRACEFUL_FATAL_ASSERT(
            !ec.failed(), "Unable to delete campaign data at " + campaignPath.string() + " err: " + ec.to_string(), );
        FWE_LOG_INFO( "Deleted streams for campaign " + campaign.first );
    }

    // mark new campaigns for creation
    std::unordered_set<std::shared_ptr<ICollectionScheme>> campaignsToCreate;
    if ( activeCollectionSchemes != nullptr )
    {
        for ( const auto &newCampaignConfig : activeCollectionSchemes->activeCollectionSchemes )
        {
            if ( newCampaignConfig == nullptr )
            {
                continue;
            }
            // skip if stream already exists
            if ( mCampaigns.find( getName( newCampaignConfig->getCampaignArn() ) ) != mCampaigns.end() )
            {
                continue;
            }
            if ( newCampaignConfig->getStoreAndForwardConfiguration().empty() )
            {
                FWE_LOG_TRACE( "Campaign " + getName( newCampaignConfig->getCampaignArn() ) +
                               " is not configured for store-and-forward" );
                continue;
            }

            campaignsToCreate.emplace( newCampaignConfig );
        }
    }

    // create streams for new campaigns
    for ( const auto &campaignConfig : campaignsToCreate )
    {

        // Identify an invalid storage locations first
        bool hasInvalidStorageLocations = false;
        for ( PartitionID pID = 0; pID < campaignConfig->getStoreAndForwardConfiguration().size(); ++pID )
        {
            const auto &storagePartition = campaignConfig->getStoreAndForwardConfiguration()[pID];
            const auto location = boost::filesystem::path{ storagePartition.storageOptions.storageLocation };
            if ( location.filename().empty() || location.filename_is_dot() || location.filename_is_dot_dot() )
            {
                FWE_LOG_ERROR( "Campaign " + getName( campaignConfig->getCampaignArn() ) +
                               " has an invalid storage location in partition " + std::to_string( pID ) );
                hasInvalidStorageLocations = true;
            }
        }
        if ( hasInvalidStorageLocations )
        {
            continue;
            // Do not continue and actually create anything on disk or in memory for this invalid campaign
        }

        for ( PartitionID pID = 0; pID < campaignConfig->getStoreAndForwardConfiguration().size(); ++pID )
        {
            // create stream partition on disk
            const auto &storagePartition = campaignConfig->getStoreAndForwardConfiguration()[pID];

            const auto location = boost::filesystem::path{ storagePartition.storageOptions.storageLocation };
            const boost::filesystem::path absoluteStorageLocation(
                boost::filesystem::path{ mPersistenceRootDir } /
                boost::filesystem::path{ getName( campaignConfig->getCampaignArn() ) } / location.filename() );
            const std::shared_ptr<aws::store::filesystem::FileSystemInterface> fs =
                std::make_shared<Aws::IoTFleetWise::Store::PosixFileSystem>( absoluteStorageLocation );

            auto storageMaxSizeBytes = static_cast<uint32_t>( storagePartition.storageOptions.maximumSizeInBytes );
            auto storageMinSegmentSize = std::min( STREAM_DEFAULT_MIN_SEGMENT_SIZE, storageMaxSizeBytes );

            auto stream = aws::store::stream::FileStream::openOrCreate( aws::store::stream::StreamOptions{
                storageMinSegmentSize,
                storageMaxSizeBytes,
                true,
                fs,
                mLogger,
                aws::store::kv::KVOptions{
                    true,
                    fs,
                    mLogger,
                    KV_STORE_IDENTIFIER,
                    KV_COMPACT_AFTER,
                },
            } );
            FWE_GRACEFUL_FATAL_ASSERT( stream.ok(), "Failed to create stream: " + stream.err().msg, );
            FWE_LOG_INFO( "Opened stream for campaign " + getName( campaignConfig->getCampaignArn() ) + " partition " +
                          std::to_string( pID ) );

            {
                // update internal state
                std::lock_guard<std::mutex> lock( mCampaignsMutex );

                std::unordered_set<SignalID> signalIDs;
                for ( auto signal : campaignConfig->getCollectSignals() )
                {
                    if ( signal.dataPartitionId == pID )
                    {
                        signalIDs.emplace( signal.signalID );
                    }
                }
                mCampaigns[getName( campaignConfig->getCampaignArn() )].config = campaignConfig;
                mCampaigns[getName( campaignConfig->getCampaignArn() )].partitions.emplace_back(
                    Partition{ pID, stream.val(), signalIDs } );
            }
        }
    }

    // cleanup any stray campaigns.
    // this could happen, for example, when a campaign is removed while FWE is not running.
    //
    // out of abundance of caution, only cleanup files that appear to be stream manager files:
    //   <campaign>/<partition/*.log
    //   <campaign>/<partition>/s
    boost::system::error_code ec;
    // look for unknown campaigns at root level
    for ( auto &rootEntry : boost::make_iterator_range(
              boost::filesystem::directory_iterator( boost::filesystem::path{ mPersistenceRootDir }, ec ) ) )
    {
        auto potentialCampaignDir = rootEntry.path();
        if ( boost::filesystem::is_directory( potentialCampaignDir, ec ) &&
             ( potentialCampaignDir.filename() != boost::filesystem::path{ "FWE_Persistency" } ) &&
             ( mCampaigns.find( potentialCampaignDir.filename().string() ) == mCampaigns.end() ) )
        {
            // look for partitions at depth 1
            for ( auto &depth1Entry : boost::make_iterator_range(
                      boost::filesystem::directory_iterator( boost::filesystem::path{ potentialCampaignDir }, ec ) ) )
            {
                auto potentialPartitionDir = depth1Entry.path();
                if ( boost::filesystem::is_directory( potentialPartitionDir, ec ) )
                {
                    // look for stream manager files at depth 2
                    for ( auto &depth2Entry : boost::make_iterator_range( boost::filesystem::directory_iterator(
                              boost::filesystem::path{ potentialPartitionDir }, ec ) ) )
                    {
                        auto potentialStreamManagerFile = depth2Entry.path();
                        if ( boost::filesystem::is_regular_file( potentialStreamManagerFile, ec ) &&
                             ( ( potentialStreamManagerFile.extension().string() == ".log" ) ||
                               potentialStreamManagerFile.filename().string() == KV_STORE_IDENTIFIER ) )
                        {
                            if ( boost::filesystem::remove( potentialStreamManagerFile, ec ) )
                            {
                                FWE_LOG_TRACE( "Removed stray file from campaign: " +
                                               potentialCampaignDir.filename().string() );
                            }
                        }
                    }
                    if ( boost::filesystem::is_empty( potentialPartitionDir, ec ) )
                    {
                        boost::filesystem::remove( potentialPartitionDir, ec );
                    }
                }
            }
        }
        if ( boost::filesystem::is_empty( potentialCampaignDir, ec ) )
        {
            boost::filesystem::remove( potentialCampaignDir, ec );
        }
    }

    removeOlderRecords();
}

void
StreamManager::removeOlderRecords()
{
    for ( auto campaign : mCampaigns )
    {
        for ( PartitionID pID = 0; pID < campaign.second.partitions.size(); ++pID )
        {
            auto partition = campaign.second.partitions[pID];
            auto minimumTTl = campaign.second.config->getStoreAndForwardConfiguration()[pID]
                                  .storageOptions.minimumTimeToLiveInSeconds;
            if ( minimumTTl > 0 )
            {
                auto ttlMs = minimumTTl * 1000;
                Timestamp removeTime = mClock->systemTimeSinceEpochMs();
                if ( removeTime >= ttlMs )
                {
                    removeTime -= ttlMs;
                }
                FWE_LOG_INFO( "Cleaning up records older than " + std::to_string( removeTime ) + " for campaign " +
                              campaign.first + " partition " + std::to_string( pID ) );
                auto totalSizeBytes = partition.stream->removeOlderRecords( static_cast<int64_t>( removeTime ) );
                TraceModule::get().addToVariable( TraceVariable::DATA_EXPIRED_BYTES, totalSizeBytes );
            }
        }
    }
}

StreamManager::ReturnCode
StreamManager::appendToStreams( const TriggeredCollectionSchemeData &data )
{
    // Holding lock for the entire method because we do not want to append to a stream which is being deleted
    // by reconfiguring the campaigns. The assumption is that this is low-contention because campaigns change
    // infrequently.
    std::lock_guard<std::mutex> lock( mCampaignsMutex );

    CampaignName campaign = getName( data.metadata.campaignArn );
    if ( mCampaigns.find( campaign ) == mCampaigns.end() )
    {
        return ReturnCode::STREAM_NOT_FOUND;
    }
    if ( data.signals.empty() )
    {
        return ReturnCode::EMPTY_DATA;
    }

    TriggeredCollectionSchemeData emptyChunk;
    emptyChunk.metadata = data.metadata;
    emptyChunk.triggerTime = data.triggerTime;
    emptyChunk.eventID = data.eventID;

    for ( const auto &partition : mCampaigns[campaign].partitions )
    {
        // each partition requires its own chunk
        TriggeredCollectionSchemeData currChunk = emptyChunk;
        uint32_t currChunkSize = 0;
        for ( auto collectedSignal : data.signals )
        {
            if ( partition.signalIDs.find( collectedSignal.signalID ) != partition.signalIDs.end() )
            {
                currChunk.signals.emplace_back( collectedSignal );
                currChunkSize++;
                if ( currChunkSize >= mTransmitThreshold )
                {
                    auto res = store( currChunk, partition );
                    if ( res != ReturnCode::SUCCESS )
                    {
                        return res;
                    }
                    currChunk = emptyChunk;
                    currChunkSize = 0;
                }
            }
        }

        // send out the chunk if we haven't already
        if ( currChunkSize > 0 )
        {
            auto res = store( currChunk, partition );
            if ( res != ReturnCode::SUCCESS )
            {
                return res;
            }
        }
    }
    return ReturnCode::SUCCESS;
}

StreamManager::ReturnCode
StreamManager::store( const TriggeredCollectionSchemeData &data, const Partition &partition )
{
    std::string dataToStore;
    if ( !serialize( data, dataToStore ) )
    {
        FWE_LOG_WARN( "Failed to serialize data. cID: " + data.metadata.collectionSchemeID +
                      " partition: " + std::to_string( partition.id ) )
        return ReturnCode::ERROR;
    }

    if ( mCampaigns[getName( data.metadata.campaignArn )].config->isCompressionNeeded() )
    {
        std::string out;
        if ( snappy::Compress( dataToStore.data(), dataToStore.size(), &out ) == 0U )
        {
            FWE_LOG_TRACE( "Error in compressing the payload. cID: " + data.metadata.collectionSchemeID +
                           " partition: " + std::to_string( partition.id ) );
            return ReturnCode::ERROR;
        }
        dataToStore = out;
    }

    auto dataBufSize = sizeof( RecordMetadata ) + dataToStore.size();
    std::vector<uint8_t> dataBuf( dataBufSize );

    auto metadata = RecordMetadata{ data.signals.size(), data.triggerTime };
    std::memcpy( dataBuf.data(), &metadata, sizeof( RecordMetadata ) );

    std::memcpy( dataBuf.data() + sizeof( RecordMetadata ), dataToStore.data(), dataToStore.size() );

    auto append_or = partition.stream->append( aws::store::common::BorrowedSlice{ dataBuf.data(), dataBuf.size() },
                                               aws::store::stream::AppendOptions{ false, true } );
    if ( !append_or.ok() )
    {
        FWE_LOG_WARN( "Failed to append data to stream. cID: " + data.metadata.collectionSchemeID +
                      " partition: " + std::to_string( partition.id ) +
                      " errCode: " + std::to_string( static_cast<uint8_t>( append_or.err().code ) ) +
                      " errMsg: " + append_or.err().msg )
        TraceModule::get().incrementVariable( TraceVariable::DATA_STORE_ERROR );
        return ReturnCode::ERROR;
    }

    TraceModule::get().addToVariable( TraceVariable::DATA_STORE_BYTES, dataToStore.size() );
    TraceModule::get().addToVariable( TraceVariable::DATA_STORE_SIGNAL_COUNT, data.signals.size() );
    return ReturnCode::SUCCESS;
}

bool
StreamManager::serialize( const TriggeredCollectionSchemeData &data, std::string &out )
{
    mProtoWriter->setupVehicleData( data, data.eventID );

    // Add signals to the protobuf
    for ( const auto &signal : data.signals )
    {
        {
            mProtoWriter->append( signal );
        }
    }

    // Add DTC info to the payload
    if ( data.mDTCInfo.hasItems() )
    {
        mProtoWriter->setupDTCInfo( data.mDTCInfo );
        const auto &dtcCodes = data.mDTCInfo.mDTCCodes;
        for ( const auto &dtc : dtcCodes )
        {
            mProtoWriter->append( dtc );
        }
    }

    return mProtoWriter->serializeVehicleData( &out );
}

StreamManager::ReturnCode
StreamManager::readFromStream( const CampaignID &cID,
                               PartitionID pID,
                               std::string &serializedData,
                               RecordMetadata &metadata,
                               std::function<void()> &checkpoint )
{
    std::shared_ptr<aws::store::stream::StreamInterface> stream;
    {
        auto campaign = getName( cID );
        std::lock_guard<std::mutex> lock( mCampaignsMutex );
        if ( mCampaigns.find( campaign ) == mCampaigns.end() )
        {
            return StreamManager::ReturnCode::STREAM_NOT_FOUND;
        }
        if ( pID >= mCampaigns[campaign].partitions.size() )
        {
            return StreamManager::ReturnCode::STREAM_NOT_FOUND;
        }
        stream = mCampaigns[campaign].partitions[pID].stream;
    }

    auto iter = stream->openOrCreateIterator( STREAM_ITER_IDENTIFIER, aws::store::stream::IteratorOptions{} );

    auto streamRecord = *iter;
    if ( !streamRecord.ok() )
    {
        if ( streamRecord.err().code == aws::store::stream::StreamErrorCode::RecordNotFound )
        {
            return StreamManager::ReturnCode::END_OF_STREAM;
        }
        FWE_LOG_WARN( "Unable to read stream record. cID: " + cID + " partition: " + std::to_string( pID ) +
                      " errCode: " + std::to_string( static_cast<uint8_t>( streamRecord.err().code ) ) +
                      " errMsg: " + streamRecord.err().msg )
        return StreamManager::ReturnCode::ERROR;
    }

    checkpoint = [stream, sequenceNumber = iter.sequence_number, cID, pID]() {
        auto err = stream->setCheckpoint( STREAM_ITER_IDENTIFIER, sequenceNumber );
        if ( !err.ok() )
        {
            // TODO is there anything else we can do besides log?
            FWE_LOG_ERROR( "Unable to checkpoint stream. cID: " + cID + " partition: " + std::to_string( pID ) +
                           " sequenceNumber: " + std::to_string( sequenceNumber ) );
        }
    };

    // metadata
    if ( streamRecord.val().data.size() >= sizeof( RecordMetadata ) )
    {
        std::copy( reinterpret_cast<const uint8_t *>( streamRecord.val().data.data() ),
                   reinterpret_cast<const uint8_t *>( streamRecord.val().data.data() ) + sizeof( RecordMetadata ),
                   reinterpret_cast<uint8_t *>( &metadata ) );
    }

    // data
    if ( streamRecord.val().data.size() > sizeof( RecordMetadata ) )
    {
        auto data = streamRecord.val().data.char_data() + sizeof( RecordMetadata );
        serializedData = std::string{ data, streamRecord.val().data.size() - sizeof( RecordMetadata ) };
    }

    return StreamManager::ReturnCode::SUCCESS;
}

bool
StreamManager::hasCampaign( const CampaignID &campaignID )
{
    auto campaign = getName( campaignID );
    std::lock_guard<std::mutex> lock( mCampaignsMutex );
    return mCampaigns.find( campaign ) != mCampaigns.end();
}

std::set<PartitionID>
StreamManager::getPartitionIdsFromCampaign( const CampaignID &campaignID )
{
    auto campaign = getName( campaignID );

    std::lock_guard<std::mutex> lock( mCampaignsMutex );
    auto partitions = mCampaigns[campaign].partitions;

    std::set<PartitionID> pIDs;

    for ( auto &partition : partitions )
    {
        pIDs.insert( partition.id );
    }
    return pIDs;
}

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
