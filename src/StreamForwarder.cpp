// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StreamForwarder.h"
#include "Clock.h"
#include "DataSenderTypes.h"
#include "LoggingModule.h"
#include "RateLimiter.h"
#include "Signal.h"
#include "StreamManager.h"
#include "TelemetryDataSender.h"
#include "Thread.h"
#include "TraceModule.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

StreamForwarder::StreamForwarder( std::shared_ptr<StreamManager> streamManager,
                                  std::shared_ptr<TelemetryDataSender> dataSender,
                                  std::shared_ptr<RateLimiter> rateLimiter,
                                  uint32_t idleTimeMs )
    : mStreamManager( std::move( streamManager ) )
    , mDataSender( std::move( dataSender ) )
    , mIdleTimeMs( idleTimeMs )
    , mRateLimiter( std::move( rateLimiter ) )
{
}

StreamForwarder::~StreamForwarder()
{
    if ( isAlive() )
    {
        stop();
    }
}

bool
StreamForwarder::start()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        FWE_LOG_ERROR( "Thread failed to start" );
    }
    else
    {
        FWE_LOG_INFO( "Thread started" );
        mThread.setThreadName( "fwDMStrUpldr" );
    }
    return mThread.isActive() && mThread.isValid();
}

bool
StreamForwarder::stop()
{
    FWE_LOG_INFO( "Stream Forwarder Thread stopping" );
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mSenderFinished.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_INFO( "Stream Forwarder Thread stopped" );
    return !mThread.isActive();
}

bool
StreamForwarder::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

bool
StreamForwarder::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
StreamForwarder::doWork( void *data )
{
    StreamForwarder *forwarder = static_cast<StreamForwarder *>( data );
    Timestamp lastTraceOutput = 0;
    size_t numberOfSignalsProcessed = 0;
    uint32_t activations = 0;

    while ( !forwarder->shouldStop() )
    {
        activations++;

        // Don't print every iteration to avoid spamming the log
        if ( forwarder->mClock->monotonicTimeSinceEpochMs() >
             ( lastTraceOutput + LoggingModule::LOG_AGGREGATION_TIME_MS ) )
        {
            FWE_LOG_TRACE( "Activations: " + std::to_string( activations ) + " Since last idling processed " +
                           std::to_string( numberOfSignalsProcessed ) + " signals" );
            activations = 0;
            numberOfSignalsProcessed = 0;
            lastTraceOutput = forwarder->mClock->monotonicTimeSinceEpochMs();
        }

        forwarder->updatePartitionsWaitingForData();

        std::vector<CampaignPartition> partitionsToSkip;
        std::map<CampaignPartition, IoTJobsEndTime> partitionsToRead;
        {
            std::lock_guard<std::mutex> lock( forwarder->mPartitionMutex );
            for ( auto const &partitions : forwarder->mPartitionsToUpload )
            {
                auto campaignPartition = partitions.first;
                if ( forwarder->partitionWaitingForData( campaignPartition ) )
                {
                    partitionsToSkip.emplace_back( campaignPartition );
                }
                else if ( forwarder->partitionIsEnabled( ( campaignPartition ) ) )
                {
                    uint64_t endTime = 0;
                    auto campaignId = campaignPartition.first;
                    if ( forwarder->mJobCampaignToEndTime.find( campaignId ) != forwarder->mJobCampaignToEndTime.end() )
                    {
                        endTime = forwarder->mJobCampaignToEndTime[campaignId];
                    }
                    partitionsToRead[campaignPartition] = endTime;
                }
            }
        }

        if ( partitionsToRead.empty() )
        {
            if ( partitionsToSkip.empty() )
            {
                FWE_LOG_TRACE( "Waiting indefinitely until campaign forwarding is requested" );
                forwarder->mWait.wait( Signal::WaitWithPredicate );
                continue;
            }
            else
            {
                // all partitions skipped, wait for data to arrive in stream
                forwarder->mWait.wait( forwarder->mIdleTimeMs );
                continue;
            }
        }

        // remove partitions that don't exist anymore
        // (happens when campaigns are removed)
        std::vector<CampaignPartition> partitionsToRemove;

        // read from partitions and forward the data to the shared data queue,
        // which in turn will be sent to cloud via a DataSender.
        for ( const auto &partition : partitionsToRead )
        {
            auto campaignPartition = partition.first;
            auto endTime = partition.second;

            if ( forwarder->shouldStop() )
            {
                return;
            }

            std::string schemeData;
            StreamManager::RecordMetadata metadata;
            std::function<void()> checkpoint;

            StreamManager::ReturnCode result = forwarder->mStreamManager->readFromStream(
                campaignPartition.first, campaignPartition.second, schemeData, metadata, checkpoint );

            if ( ( endTime != 0 ) && ( metadata.triggerTime >= static_cast<Timestamp>( endTime ) ) )
            {
                forwarder->checkIfJobCompleted( campaignPartition );
            }
            else if ( result == StreamManager::ReturnCode::SUCCESS )
            {
                if ( !forwarder->mRateLimiter->consumeToken() )
                {
                    forwarder->mWait.wait( forwarder->mIdleTimeMs );
                    continue;
                }

                numberOfSignalsProcessed += metadata.numSignals;

                FWE_LOG_INFO( "Processing campaign " + campaignPartition.first + " partition " +
                              std::to_string( campaignPartition.second ) );
                forwarder->mDataSender->processSerializedData(
                    schemeData,
                    [checkpoint, forwarder, metadata]( bool success, std::shared_ptr<const DataToPersist> unused ) {
                        static_cast<void>( unused );
                        if ( success )
                        {
                            checkpoint();
                            TraceModule::get().addToVariable( TraceVariable::DATA_FORWARD_SIGNAL_COUNT,
                                                              metadata.numSignals );
                        }
                        else
                        {
                            TraceModule::get().incrementVariable( TraceVariable::DATA_FORWARD_ERROR );
                        }
                        forwarder->mSenderFinished.notify();
                    } );
                forwarder->mSenderFinished.wait( Signal::WaitWithPredicate );
            }
            else if ( result == StreamManager::ReturnCode::END_OF_STREAM )
            {
                forwarder->checkIfJobCompleted( campaignPartition );
                forwarder->mPartitionsWaitingForData[campaignPartition] =
                    forwarder->mClock->monotonicTimeSinceEpochMs() + 1000;
            }
            else if ( result == StreamManager::ReturnCode::STREAM_NOT_FOUND )
            {
                partitionsToRemove.emplace_back( campaignPartition );
            }
            else if ( result == StreamManager::ReturnCode::ERROR )
            {
                FWE_LOG_ERROR( "Unable to read from stream. campaignID: " + campaignPartition.first +
                               ", partitionID: " + std::to_string( campaignPartition.second ) );
            }
        }

        for ( const auto &campaignPartition : partitionsToRemove )
        {
            FWE_LOG_INFO( "Removing partition. campaignID: " + campaignPartition.first +
                          ", partitionID: " + std::to_string( campaignPartition.second ) );
            std::lock_guard<std::mutex> lock( forwarder->mPartitionMutex );
            if ( forwarder->mPartitionsToUpload.erase( campaignPartition ) > 0 )
            {
                FWE_LOG_TRACE( "Stream for partition not found, removing. campaignID: " + campaignPartition.first +
                               ", partitionID: " + std::to_string( campaignPartition.second ) )
            }
        }

        forwarder->mWait.wait( forwarder->mIdleTimeMs );
    }
}

bool
StreamForwarder::partitionIsEnabled( const CampaignPartition &campaignPartition )
{
    auto partition = mPartitionsToUpload.find( campaignPartition );
    return ( partition != mPartitionsToUpload.end() ) && ( !partition->second.enabled.empty() );
}

void
StreamForwarder::checkIfJobCompleted( const CampaignPartition &campaignPartition )
{
    std::unique_lock<std::mutex> lock( mPartitionMutex );

    if ( mJobCampaignToPartitions.find( campaignPartition.first ) != mJobCampaignToPartitions.end() )
    {
        FWE_LOG_TRACE( "Cancel IoT Job forward for campaign: " + campaignPartition.first +
                       ",  partitionID: " + std::to_string( campaignPartition.second ) );
        // This means that the campaign and subsequent partition that we are looking at is for an IoT
        // Job Since we have reached END_OF_STREAM or hit endTime, remove this partition
        mJobCampaignToPartitions[campaignPartition.first].erase( campaignPartition.second );

        // This is equivalent to calling cancelForward
        mPartitionsToUpload[campaignPartition].enabled.erase( Source::IOT_JOB );

        if ( mJobCampaignToPartitions[campaignPartition.first].empty() )
        {
            // all the campaign partitions have reached end of data stream so complete the Iot Job
            mJobCampaignToPartitions.erase( campaignPartition.first );
            mJobCampaignToEndTime.erase( campaignPartition.first );

            // don't hold the mPartitionMutex in the job completion callback
            lock.unlock();

            if ( mJobCompletionCallback )
            {
                // Triggers IotJobDataRequestHandler to update job status
                FWE_LOG_TRACE( "Notifying IoTJobDataRequestHandler that a job has completed uploading data" );
                mJobCompletionCallback( campaignPartition.first );
            }
        }
    }
}

void
StreamForwarder::beginJobForward( const CampaignID &cID, uint64_t endTime )
{
    auto partitions = mStreamManager->getPartitionIdsFromCampaign( cID );
    for ( auto &pID : partitions )
    {
        FWE_LOG_TRACE( "Forward requested. campaignID: " + cID + ", partitionID: " + std::to_string( pID ) +
                       " endTime: " + std::to_string( endTime ) + " source: IOT_JOB" );
        std::lock_guard<std::mutex> lock( mPartitionMutex );
        if ( mJobCampaignToPartitions.find( cID ) != mJobCampaignToPartitions.end() &&
             ( mJobCampaignToPartitions[cID].find( pID ) != mJobCampaignToPartitions[cID].end() ) &&
             ( mJobCampaignToEndTime.find( cID ) != mJobCampaignToEndTime.end() ) )
        {
            // Note: we are checking partition to avoid calculating the maxEndtime for only one Job beginJobForward call
            // A job has already targeted this campaign cID. Take the max endTime unless one of the endTimes is 0
            uint64_t maxEndTime = 0;
            uint64_t currentEndTime = mJobCampaignToEndTime[cID];
            if ( ( endTime != 0 ) && ( currentEndTime != 0 ) )
            {
                maxEndTime = currentEndTime < endTime ? endTime : currentEndTime;
            }
            mJobCampaignToEndTime[cID] = maxEndTime;
        }
        else
        {
            mJobCampaignToEndTime[cID] = endTime;
        }
        mJobCampaignToPartitions[cID].insert( pID );
        mPartitionsToUpload[{ cID, pID }].enabled.insert( Source::IOT_JOB );
    }
    mWait.notify();
}

void
StreamForwarder::beginForward( const CampaignID &cID, PartitionID pID, Source source )
{
    std::lock_guard<std::mutex> lock( mPartitionMutex );
    if ( mPartitionsToUpload[{ cID, pID }].enabled.insert( source ).second )
    {
        FWE_LOG_TRACE( "Forward requested. campaignID: " + cID + ", partitionID: " + std::to_string( pID ) );
        mWait.notify();
    }
}

void
StreamForwarder::cancelForward( const CampaignID &cID, PartitionID pID, Source source )
{
    std::lock_guard<std::mutex> lock( mPartitionMutex );
    if ( mPartitionsToUpload[{ cID, pID }].enabled.find( source ) != mPartitionsToUpload[{ cID, pID }].enabled.end() )
    {
        if ( mPartitionsToUpload[{ cID, pID }].enabled.erase( source ) > 0 )
        {
            FWE_LOG_TRACE( "Forward cancellation requested. campaignID: " + cID +
                           ", partitionID: " + std::to_string( pID ) );
        }
        if ( mPartitionsToUpload[{ cID, pID }].enabled.empty() )
        {
            mPartitionsToUpload.erase( { cID, pID } );
        }
    }
}

void
StreamForwarder::registerJobCompletionCallback( JobCompletionCallback callback )
{
    mJobCompletionCallback = std::move( callback );
}

void
StreamForwarder::updatePartitionsWaitingForData()
{
    std::vector<CampaignPartition> expired;
    for ( auto &partition : mPartitionsWaitingForData )
    {
        if ( partition.second > mClock->timeSinceEpoch().monotonicTimeMs )
        {
            expired.emplace_back( partition.first );
        }
    }
    for ( auto &partition : expired )
    {
        mPartitionsWaitingForData.erase( partition );
    }
}

bool
StreamForwarder::partitionWaitingForData( const CampaignPartition &campaignPartition )
{
    return mPartitionsWaitingForData.find( campaignPartition ) != mPartitionsWaitingForData.end();
}

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
