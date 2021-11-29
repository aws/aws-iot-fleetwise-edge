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
#include "Listener.h"
#include "NetworkChannelBridgeListener.h"
#include "datatypes/CANRawMessage.h"
#include "datatypes/NetworkChannelDataTypes.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
using namespace Aws::IoTFleetWise::Platform;
typedef boost::lockfree::spsc_queue<CANRawMessage> CircularBuffer;
typedef std::shared_ptr<CircularBuffer> CircularBufferPtr;

/**
 * @brief Abstract Network Channel Interface. Each Network bus backend Impl needs to derive from the class.
 * All the functions below should be thread safe i.e. the API allows concurrent calls to the same
 * routine from different thread. Each of the implementation of this class should have exactly
 * ONE THREAD to manage its state.
 */
class INetworkChannelBridge : public ThreadListeners<NetworkChannelBridgeListener>
{
public:
    virtual ~INetworkChannelBridge() = default;

    /**
     * @brief Initializes the Network Backend. Implementation varies among Network IFs.
     * @param bufferSize maximum size of the circular buffer at a time.
     * @param idleTimeMs if no new data is available sleep for this amount of milliseconds
     * @return True if the Backend has been setup correctly.
     */
    virtual bool init( uint32_t bufferSize, uint32_t idleTimeMs ) = 0;

    /**
     * @brief Creates the Bus Thread and establishes a Connection to the Network Bus PHY.
     * @return True if the thread is active and connection has been setup.
     */
    virtual bool connect() = 0;

    /**
     * @brief Stops the thread and disconnect from the CAN Bus PHY.
     * @return True if the thread is stopped and connection has been closed.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Checks if the connection to the bus is healthy and data
     * is being consumed from the Network PHY.
     *  Implementation varies among CAN IFs.
     * @return True if the connection is healthy.
     */
    virtual bool isAlive() = 0;

    /**
     * @brief Ask the channel to resume the data acquisition from the Network.
     */
    virtual void resumeDataAcquisition() = 0;

    /**
     * @brief Ask the channel to suspend the data acquisition from the Network.
     */
    virtual void suspendDataAcquisition() = 0;

    /**
     * @brief Handle of the Network Bus circular buffer. Uses of the Bus
     * can use this object to consume data. The buffer can ONLY be consume
     * from one single consumer thread.
     * @return shared object pointer to the circular buffer.
     */
    inline CircularBufferPtr
    getBuffer()
    {
        return mCircularBuffPtr;
    }

    /**
     * @return the type of the channel PHY.
     */
    inline NetworkChannelType
    getChannelType()
    {
        return mType;
    }

    /**
     * @return the unique ID of the channel.
     */
    inline NetworkChannelID
    getChannelID()
    {
        return mID;
    }

    /**
     * @return the channel Interface name
     */
    inline NetworkChannelIfName
    getChannelIfName()
    {
        return mIfName;
    }

    /**
     * @return the Network Protocol Type
     */
    inline NetworkChannelProtocol
    getChannelProtocol()
    {
        return mNetworkProtocol;
    }

protected:
    /**
     * @brief Thread safe Channel ID generator
     * @return returns a unique identifier of a channel.
     */
    uint32_t
    generateChannelID()
    {
        static std::atomic<uint32_t> channelID( INVALID_CHANNEL_ID );
        return ++channelID;
    }

    CircularBufferPtr mCircularBuffPtr;
    NetworkChannelType mType;
    NetworkChannelID mID;
    NetworkChannelIfName mIfName;
    NetworkChannelProtocol mNetworkProtocol;
};
typedef std::shared_ptr<INetworkChannelBridge> NetworkChannelPtr;
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
