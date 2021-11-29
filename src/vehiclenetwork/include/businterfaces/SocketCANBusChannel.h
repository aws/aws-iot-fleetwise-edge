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
#include "ClockHandler.h"
#include "INetworkChannelBridge.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <iostream>

using namespace Aws::IoTFleetWise::Platform;

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
/**
 * @brief Linux SocketCAN Bus implementation. Uses Raw Sockets to listen to CAN
 * data on 1 single CAN IF.
 */
class SocketCANBusChannel : public INetworkChannelBridge
{
public:
    static const int PARALLEL_RECEIVED_FRAMES_FROM_KERNEL = 10; // If it get to big move from stack to heap
    /**
     * @brief Bus Constructor.
     * @param socketCanInterfaceName interface name e.g. can0
     * @param useKernelTimestamp the kernel time which is normally more precise will be used
     */
    SocketCANBusChannel( const std::string &socketCanInterfaceName, bool useKernelTimestamp = true );
    virtual ~SocketCANBusChannel();

    bool init( uint32_t bufferSize, uint32_t idleTimeMsIn ) override;

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
    size_t queueSize();

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mShouldSleep{ false };
    mutable std::recursive_mutex mThreadMutex;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{};
    Platform::Signal mWait;
    uint32_t mIdleTimeMs;
    uint64_t receivedMessages;
    uint64_t discardedMessages;
    bool mUseKernelTimestamp;
    std::atomic<timestampT> mResumeTime;
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
