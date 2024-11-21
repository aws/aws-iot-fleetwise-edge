// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "RateLimiter.h"
#include "Signal.h"
#include "StreamManager.h"
#include "TelemetryDataSender.h"
#include "Thread.h"
#include "TimeTypes.h"
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
    StreamForwarder( std::shared_ptr<StreamManager> streamManager,
                     std::shared_ptr<TelemetryDataSender> dataSender,
                     std::shared_ptr<RateLimiter> rateLimiter,
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
    void beginForward( const CampaignID &cID, PartitionID pID, Source source );
    void cancelForward( const CampaignID &cID, PartitionID pID, Source source );

    using JobCompletionCallback = std::function<void( const CampaignID & )>;
    virtual void registerJobCompletionCallback( JobCompletionCallback callback );

    bool start();
    bool stop();
    bool isAlive();
    bool shouldStop() const;

    static void doWork( void *data );

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

    std::shared_ptr<StreamManager> mStreamManager;
    std::shared_ptr<TelemetryDataSender> mDataSender;

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
    std::shared_ptr<RateLimiter> mRateLimiter;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    Signal mSenderFinished;
};

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
