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
#include "IVehicleDataConsumer.h"
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
using namespace Aws::IoTFleetWise::Platform::Linux;
/**
 * @brief CAN Network Data Consumer impl, outputs data to an in-memory buffer.
 *        Operates in Polling mode.
 */
class CANDataConsumer : public IVehicleDataConsumer
{
public:
    CANDataConsumer() = default;
    ~CANDataConsumer() override;

    CANDataConsumer( const CANDataConsumer & ) = delete;
    CANDataConsumer &operator=( const CANDataConsumer & ) = delete;
    CANDataConsumer( CANDataConsumer && ) = delete;
    CANDataConsumer &operator=( CANDataConsumer && ) = delete;

    bool init( VehicleDataSourceID canChannelID, SignalBufferPtr signalBufferPtr, uint32_t idleTimeMs ) override;

    bool connect() override;

    bool disconnect() override;

    bool isAlive() final;

    void resumeDataConsumption( ConstDecoderDictionaryConstPtr &dictionary ) override;

    void suspendDataConsumption() override;

    /**
     * @brief This setter is specific to CAN Bus data consumers and captures the raw
     * CAN Frames if wanted.
     * @param canBufferPtr- RAW CAN Buffer
     */
    inline void
    setCANBufferPtr( CANBufferPtr canBufferPtr )
    {
        mCANBufferPtr = canBufferPtr;
    }

    /**
     * @brief Handle of the CAN Raw Output Buffer. This buffer shared between Collection Engine
     * and Vehicle Data CAN Consumer.
     * @return shared object pointer to the CAN Frame buffer.
     */
    inline CANBufferPtr
    getCANBufferPtr() const
    {
        return mCANBufferPtr;
    }

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
    mutable std::mutex mThreadMutex;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::mutex mDecoderDictMutex;
    std::unique_ptr<CANDecoder> mCANDecoder;
    Platform::Linux::Signal mWait;
    uint32_t mIdleTime{ DEFAULT_THREAD_IDLE_TIME_MS };
    static constexpr uint32_t DEFAULT_THREAD_IDLE_TIME_MS = 1000;
    // Raw CAN Frame Buffer shared pointer
    CANBufferPtr mCANBufferPtr;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
