// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamManager.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

class StreamForwarder
{
public:
    StreamForwarder( StreamManager &streamManager,
                     TelemetryDataSender &dataSender,
                     RateLimiter &rateLimiter,
                     uint32_t idleTimeMs = 50 );
    virtual ~StreamForwarder();

    StreamForwarder( const StreamForwarder & ) = delete;
    StreamForwarder &operator=( const StreamForwarder & ) = delete;
    StreamForwarder( StreamForwarder && ) = delete;
    StreamForwarder &operator=( StreamForwarder && ) = delete;

    enum struct Source
    {
        IOT_JOB,
        CONDITION
    };

    void beginJobForward( const CampaignID &cID, uint64_t endTime );
    virtual void beginForward( const CampaignID &cID, PartitionID pID, Source source );
    virtual void cancelForward( const CampaignID &cID, PartitionID pID, Source source );

    using JobCompletionCallback = std::function<void( const CampaignID & )>;
    virtual void registerJobCompletionCallback( JobCompletionCallback callback );

    bool start();
    bool stop();
    bool isAlive();
    bool shouldStop() const;

    void doWork();

private:
    using IoTJobsEndTime = uint64_t;
    using CampaignPartition = std::pair<CampaignID, PartitionID>;

    bool partitionIsEnabled( const CampaignPartition &campaignPartition );
    void checkIfJobCompleted( const CampaignPartition &campaignPartition );
    void updatePartitionsWaitingForData();
    bool partitionWaitingForData( const CampaignPartition &campaignPartition );

    struct PartitionToUpload
    {
        std::set<Source> enabled;
    };

    StreamManager &mStreamManager;
    TelemetryDataSender &mDataSender;

    std::map<CampaignPartition, PartitionToUpload> mPartitionsToUpload;
    std::map<CampaignPartition, Timestamp> mPartitionsWaitingForData;
    std::map<CampaignID, std::set<PartitionID>> mJobCampaignToPartitions;
    std::map<CampaignID, uint64_t> mJobCampaignToEndTime;
    std::mutex mPartitionMutex;

    JobCompletionCallback mJobCompletionCallback;

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;
    Signal mWait;
    uint32_t mIdleTimeMs;
    RateLimiter &mRateLimiter;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    Signal mSenderFinished;
};

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
