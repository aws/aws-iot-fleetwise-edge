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

// Includes

#include "CANDecoder.h"
#include "ClockHandler.h"
#include "INetworkChannelConsumer.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform;
/**
 * @brief Network Channel Consumer impl, outputs data to an in-memory buffer.
 *        Operates in Polling mode.
 */
class NetworkChannelConsumer : public INetworkChannelConsumer
{
public:
    NetworkChannelConsumer();
    virtual ~NetworkChannelConsumer();

    bool init( uint8_t canChannelID,
               SignalBufferPtr signalBufferPtr,
               CANBufferPtr canBufferPtr,
               uint32_t idleTimeMs ) override;

    bool connect() override;

    bool disconnect() override;

    bool isAlive() final;

    void resumeDataConsumption( ConstDecoderDictionaryConstPtr &dictionary ) override;

    void suspendDataConsumption() override;

private:
    // Start the  thread
    bool start();
    // Stop the  thread
    bool stop();
    // Intercepts stop signals.
    bool shouldStop() const;
    // Intercepts sleep signals.
    bool shouldSleep() const;
    // Main work function. consumes messages from the channel Circular buffer.
    // Decodes the messages and puts the results in the output buffer.
    static void doWork( void *data );

    bool switchCollectionSchemeIfNeeded();

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mShouldSleep{ false };
    mutable std::recursive_mutex mThreadMutex;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::mutex mDecoderDictMutex;
    std::unique_ptr<CANDecoder> mCANDecoder;
    Platform::Signal mWait;
    uint32_t mIdleTime;
    static const uint32_t DEFAULT_THREAD_IDLE_TIME_MS = 1000;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
