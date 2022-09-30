/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "AbstractVehicleDataSource.h"
#include "ClockHandler.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <iostream>
#include <net/if.h>

using namespace Aws::IoTFleetWise::Platform::Linux;

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

// This timestamp is used when uploading data to the cloud
enum class CAN_TIMESTAMP_TYPE
{
    KERNEL_SOFTWARE_TIMESTAMP, // default and the best option in most scenarios
    KERNEL_HARDWARE_TIMESTAMP, // is not necassary a unix epoch timestamp which will lead to problems and records
                               // potentially rejected by cloud
    POLLING_TIME, // fallback if selected value is 0. can lead to multiple can frames having the same timestamp and so
                  // being dropped by cloud
};

inline bool
stringToCanTimestampType( std::string const &timestampType, CAN_TIMESTAMP_TYPE &outTimestampType )
{
    if ( timestampType == "Software" )
    {
        outTimestampType = CAN_TIMESTAMP_TYPE::KERNEL_SOFTWARE_TIMESTAMP;
    }
    else if ( timestampType == "Hardware" )
    {
        outTimestampType = CAN_TIMESTAMP_TYPE::KERNEL_HARDWARE_TIMESTAMP;
    }
    else if ( timestampType == "Polling" )
    {
        outTimestampType = CAN_TIMESTAMP_TYPE::POLLING_TIME;
    }
    else
    {
        return false;
    }
    return true;
}
/**
 * @brief Linux CAN Bus implementation. Uses Raw Sockets to listen to CAN
 * data on 1 single CAN IF.
 */
class CANDataSource : public AbstractVehicleDataSource
{
public:
    static constexpr int PARALLEL_RECEIVED_FRAMES_FROM_KERNEL = 10;
    static constexpr int DEFAULT_THREAD_IDLE_TIME_MS = 1000;

    /**
     * @brief Data Source Constructor.
     * @param timestampTypeToUse which timestamp type should be used to tag the can frames, this timestamp will be
     * visible in the cloud
     */
    CANDataSource( CAN_TIMESTAMP_TYPE timestampTypeToUse );
    CANDataSource();

    ~CANDataSource() override;

    CANDataSource( const CANDataSource & ) = delete;
    CANDataSource &operator=( const CANDataSource & ) = delete;
    CANDataSource( CANDataSource && ) = delete;
    CANDataSource &operator=( CANDataSource && ) = delete;

    bool init( const std::vector<VehicleDataSourceConfig> &sourceConfigs ) override;
    bool connect() override;

    bool disconnect() override;

    bool isAlive() final;

    void resumeDataAcquisition() override;

    void suspendDataAcquisition() override;

private:
    // Start the bus thread
    bool start();
    // Stop the bus thread
    bool stop();
    // atomic state of the bus. If true, we should stop
    bool shouldStop() const;
    // Intercepts sleep signals.
    bool shouldSleep() const;
    // Main work function. Listens on the socket for CAN Messages
    // and push data to the circular buffer.
    static void doWork( void *data );
    // Current non deterministic size of the circular buffer
    size_t queueSize() const;

    Timestamp extractTimestamp( struct msghdr *msgHeader );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mShouldSleep{ false };
    mutable std::mutex mThreadMutex;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{ -1 };
    Platform::Linux::Signal mWait;
    uint32_t mIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };
    uint64_t receivedMessages{ 0 };
    uint64_t discardedMessages{ 0 };
    CAN_TIMESTAMP_TYPE mTimestampTypeToUse{ CAN_TIMESTAMP_TYPE::KERNEL_SOFTWARE_TIMESTAMP };
    std::atomic<Timestamp> mResumeTime{ 0 };
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
