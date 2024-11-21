// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "DataSenderProtoWriter.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "SignalTypes.h"
#include "StoreLogger.h"
#include "TimeTypes.h"
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
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

using CampaignID = std::string;   // full campaign arn
using CampaignName = std::string; // last part of a campaign id
using PartitionID = uint32_t;

class StreamManager
{

public:
    static const PartitionID DEFAULT_PARTITION;
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

    StreamManager( std::string persistenceRootDir,
                   std::shared_ptr<DataSenderProtoWriter> protoWriter,
                   uint32_t transmitThreshold );
    virtual ~StreamManager() = default;
    void onChangeCollectionSchemeList( const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes );

    virtual ReturnCode appendToStreams( const TriggeredCollectionSchemeData &data );

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
        auto lastArnSeperator = campaignID.find_last_of( '/' );
        if ( ( lastArnSeperator == CampaignID::npos ) || ( lastArnSeperator + 1 == campaignID.size() ) )
        {
            return campaignID;
        }
        return campaignID.substr( lastArnSeperator + 1 );
    }

private:
    struct Partition
    {
        PartitionID id;
        std::shared_ptr<aws::store::stream::StreamInterface> stream;
        std::unordered_set<SignalID> signalIDs;
    };

    struct Campaign
    {
        std::vector<Partition> partitions;
        std::shared_ptr<ICollectionScheme> config;
    };

    std::shared_ptr<Aws::IoTFleetWise::DataSenderProtoWriter> mProtoWriter;
    uint32_t mTransmitThreshold;

    std::string mPersistenceRootDir;
    std::shared_ptr<Aws::IoTFleetWise::Store::Logger> mLogger;

    std::map<CampaignName, Campaign> mCampaigns;
    std::mutex mCampaignsMutex;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    bool serialize( const TriggeredCollectionSchemeData &data, std::string &out );
    void removeOlderRecords();

    ReturnCode store( const TriggeredCollectionSchemeData &data, const Partition &partition );
};

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
