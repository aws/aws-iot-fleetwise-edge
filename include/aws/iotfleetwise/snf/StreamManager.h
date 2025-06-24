// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/snf/StoreLogger.h"
#include <aws/store/stream/stream.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

namespace Store
{

using CampaignID = std::string;   // full campaign arn
using CampaignName = std::string; // last part of a campaign id

struct Partition
{
    PartitionID id;
    std::shared_ptr<aws::store::stream::StreamInterface> stream;
    std::unordered_set<SignalID> signalIDs;
};

class StreamManager
{

public:
    static const std::string STREAM_ITER_IDENTIFIER;
    static const std::string KV_STORE_IDENTIFIER;
    static const int32_t KV_COMPACT_AFTER;
    static const uint32_t STREAM_DEFAULT_MIN_SEGMENT_SIZE;

    enum class ReturnCode
    {
        // The operation completed successfully.
        SUCCESS = 0,
        // The stream associated with the operation is not part of
        // any campaign known to Stream Manager.
        STREAM_NOT_FOUND,
        // The stream iterator has reached the current end of the stream.
        // Please note that records can be added to the stream at any time,
        // so operations that return this code will likely eventually succeed.
        END_OF_STREAM,
        // If data is empty (no signal information), it will not be stored.
        EMPTY_DATA,
        // A catch-all status indicating the operation failed.
        ERROR
    };

    struct RecordMetadata
    {
        size_t numSignals;
        Timestamp triggerTime;
    };

    StreamManager( std::string persistenceRootDir );
    virtual ~StreamManager() = default;
    void onChangeCollectionSchemeList( std::shared_ptr<const ActiveCollectionSchemes> activeCollectionSchemes );

    virtual ReturnCode appendToStreams( const TelemetryDataToPersist &data );

    /**
     * Read a record directly from the stream for a specific campaign and partition.
     *
     * @param cID campaign id
     * @param pID partition id
     * @param serializedData record data
     * @param metadata record metadata
     * @param checkpoint function that checkpoints the stream and advances the iterator to the next record
     * @return code describing the result
     */
    virtual ReturnCode readFromStream( const CampaignID &cID,
                                       PartitionID pID,
                                       std::string &serializedData,
                                       RecordMetadata &metadata,
                                       std::function<void()> &checkpoint );

    virtual bool hasCampaign( const CampaignID &campaignID );

    virtual std::set<PartitionID> getPartitionIdsFromCampaign( const CampaignID &campaignID );

    static CampaignName
    getName( const CampaignID &campaignID )
    {
        auto lastArnSeparator = campaignID.find_last_of( '/' );
        if ( ( lastArnSeparator == CampaignID::npos ) || ( lastArnSeparator + 1 == campaignID.size() ) )
        {
            return campaignID;
        }
        return campaignID.substr( lastArnSeparator + 1 );
    }

    virtual std::shared_ptr<const std::vector<Partition>>
    getPartitions( const std::string &campaignArn )
    {
        CampaignName campaignName = getName( campaignArn );

        std::lock_guard<std::mutex> lock( mCampaignsMutex );
        auto it = mCampaigns.find( campaignName );
        if ( it == mCampaigns.end() )
        {
            return {};
        }
        return it->second.partitions;
    }

private:
    struct Campaign
    {
        std::shared_ptr<const std::vector<Partition>> partitions;
        std::shared_ptr<ICollectionScheme> config;
    };

    std::string mPersistenceRootDir;
    std::shared_ptr<Aws::IoTFleetWise::Store::Logger> mLogger;

    std::map<CampaignName, Campaign> mCampaigns;
    std::mutex mCampaignsMutex;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    void removeOlderRecords();

    static ReturnCode store( const TelemetryDataToPersist &data, const Partition &partition );
};

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
