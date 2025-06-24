// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/TraceModule.h"
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamManager.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <json/json.h>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

StreamForwarder::StreamForwarder( StreamManager &streamManager,
                                  TelemetryDataSender &dataSender,
                                  RateLimiter &rateLimiter,
                                  uint32_t idleTimeMs )
    : mStreamManager( streamManager )
    , mDataSender( dataSender )
    , mIdleTimeMs( idleTimeMs )
    , mRateLimiter( rateLimiter )
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
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
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
StreamForwarder::doWork()
{
    Timestamp lastTraceOutput = 0;
    size_t numberOfSignalsProcessed = 0;
    uint32_t activations = 0;

    while ( !shouldStop() )
    {
        activations++;

        // Don't print every iteration to avoid spamming the log
        if ( mClock->monotonicTimeSinceEpochMs() > ( lastTraceOutput + LoggingModule::LOG_AGGREGATION_TIME_MS ) )
        {
            FWE_LOG_TRACE( "Activations: " + std::to_string( activations ) + " Since last idling processed " +
                           std::to_string( numberOfSignalsProcessed ) + " signals" );
            activations = 0;
            numberOfSignalsProcessed = 0;
            lastTraceOutput = mClock->monotonicTimeSinceEpochMs();
        }

        updatePartitionsWaitingForData();

        std::vector<CampaignPartition> partitionsToSkip;
        std::map<CampaignPartition, IoTJobsEndTime> partitionsToRead;
        {
            std::lock_guard<std::mutex> lock( mPartitionMutex );
            for ( auto const &partitions : mPartitionsToUpload )
            {
                auto campaignPartition = partitions.first;
                if ( partitionWaitingForData( campaignPartition ) )
                {
                    partitionsToSkip.emplace_back( campaignPartition );
                }
                else if ( partitionIsEnabled( ( campaignPartition ) ) )
                {
                    uint64_t endTime = 0;
                    const auto &campaignId = campaignPartition.first;
                    if ( mJobCampaignToEndTime.find( campaignId ) != mJobCampaignToEndTime.end() )
                    {
                        endTime = mJobCampaignToEndTime[campaignId];
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
                mWait.wait( Signal::WaitWithPredicate );
                continue;
            }
            else
            {
                // all partitions skipped, wait for data to arrive in stream
                mWait.wait( mIdleTimeMs );
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

            if ( shouldStop() )
            {
                return;
            }

            std::string schemeData;
            StreamManager::RecordMetadata metadata;
            std::function<void()> checkpoint;

            StreamManager::ReturnCode result = mStreamManager.readFromStream(
                campaignPartition.first, campaignPartition.second, schemeData, metadata, checkpoint );

            if ( ( endTime != 0 ) && ( metadata.triggerTime >= static_cast<Timestamp>( endTime ) ) )
            {
                checkIfJobCompleted( campaignPartition );
            }
            else if ( result == StreamManager::ReturnCode::SUCCESS )
            {
                if ( !mRateLimiter.consumeToken() )
                {
                    mWait.wait( mIdleTimeMs );
                    continue;
                }

                numberOfSignalsProcessed += metadata.numSignals;

                FWE_LOG_INFO( "Processing campaign " + campaignPartition.first + " partition " +
                              std::to_string( campaignPartition.second ) );
                Json::Value jsonMetadata;
                mDataSender.processPersistedData(
                    reinterpret_cast<const uint8_t *>( schemeData.data() ),
                    schemeData.size(),
                    jsonMetadata,
                    [this, checkpoint = std::move( checkpoint ), metadata]( bool success ) {
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
                        mSenderFinished.notify();
                    } );
                mSenderFinished.wait( Signal::WaitWithPredicate );
            }
            else if ( result == StreamManager::ReturnCode::END_OF_STREAM )
            {
                checkIfJobCompleted( campaignPartition );
                mPartitionsWaitingForData[campaignPartition] = mClock->monotonicTimeSinceEpochMs() + 1000;
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
            std::lock_guard<std::mutex> lock( mPartitionMutex );
            if ( mPartitionsToUpload.erase( campaignPartition ) > 0 )
            {
                FWE_LOG_TRACE( "Stream for partition not found, removing. campaignID: " + campaignPartition.first +
                               ", partitionID: " + std::to_string( campaignPartition.second ) )
            }
        }

        mWait.wait( mIdleTimeMs );
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
            // coverity[LOCK:FALSE] unique_lock's destructor won't try to unlock again
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
    auto partitions = mStreamManager.getPartitionIdsFromCampaign( cID );
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
