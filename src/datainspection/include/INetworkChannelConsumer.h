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

#include "CANDataTypes.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "businterfaces/INetworkChannelBridge.h"
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::DataManagement;
// Identifier of the Consumer
typedef uint32_t NetworkChannelConsumerID;
const NetworkChannelConsumerID INVALID_CHANNEL_CONSUMER_ID = 0;

/**
 * @brief Abstract Network Channel Consumer Interface. A network channel consumer is
 * bound to only one Network Channel e.g. CAN Bus. It receives the raw packets from
 * the Network Channel and decodes/filters the data according to the decoder dictionary.
 * The result will be put into three output buffers for inspection engine to process in a fanout
 * fashion. Among the three output buffers, the signal Buffer and CAN Frame buffer are multiple
 * producer single consumer buffer while DTC is single producer single consumer buffer.
 * The life cycle of this Consumer is fully managed by the NetworkChannelBinder worker thread.
 * Only instantiation is allowed on startup.
 */
class INetworkChannelConsumer
{
public:
    INetworkChannelConsumer() = default;

    virtual ~INetworkChannelConsumer() = default;

    /**
     * @brief Initializes the Network Channel Consumer.
     * @param canChannelID the CAN Channel ID
     * @param signalBufferPtr Signal Buffer shared pointer.
     * @param canBufferPtr CAN Frame Buffer shared pointer.
     * @param idleTimeMs if no new data is available sleep for this amount of milliseconds
     * @return True if the Consumer has been setup correctly.
     */
    virtual bool init( uint8_t canChannelID,
                       SignalBufferPtr signalBufferPtr,
                       CANBufferPtr canBufferPtr,
                       uint32_t idleTimeMs ) = 0;

    /**
     * @brief Creates and starts the worker thread of the consumer to start
     * consuming the data from the input buffer.
     * @return True if the thread is active and consumption has started.
     */
    virtual bool connect() = 0;

    /**
     * @brief Stops the thread and disconnect from the Network Channel input buffer.
     * @return True if the thread is stopped and connection has been closed.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Checks that the worker thread is healthy and consuming data.
     * @return True if the consumption is healthy.
     */
    virtual bool isAlive() = 0;

    /**
     * @brief Ask the consumer to resume the decoding of incoming data
     * with the provided dictionary. If the dictionary is null or corrupt,
     * the consumer should self interrupt and continue to sleep.
     * @param dictionary const pointer to the decoder manifest.
     */
    virtual void resumeDataConsumption( ConstDecoderDictionaryConstPtr &dictionary ) = 0;

    /**
     * @brief Ask the consumer to stop the decoding of incoming data.
     * This is typically the case when the source of the data is either interrupted or disconnected.
     */
    virtual void suspendDataConsumption() = 0;

    /**
     * @brief Handle of the Signal Output Buffer. This buffer shared between Collection Engine
     * and Network Channel Consumer.
     * @return shared object pointer to the Signal buffer.
     */
    inline SignalBufferPtr
    getSignalBufferPtr() const
    {
        return mSignalBufferPtr;
    }

    /**
     * @brief Handle of the CAN Raw Output Buffer. This buffer shared between Collection Engine
     * and Network Channel Consumer.
     * @return shared object pointer to the CAN Frame buffer.
     */
    inline CANBufferPtr
    getCANBufferPtr() const
    {
        return mCANBufferPtr;
    }

    /**
     * @brief Set the consumption buffer of the consumer.
     */
    inline void
    setInputBuffer( CircularBufferPtr producerBuffer )
    {
        mInputBuffer = producerBuffer;
    }

    /**
     * @return the unique ID of the consumer.
     */
    inline NetworkChannelConsumerID
    getConsumerID()
    {
        return mID;
    }

    /**
     * @brief Set the consumption buffer of the consumer.
     */
    inline void
    setChannelMetadata( std::tuple<NetworkChannelType, NetworkChannelProtocol, NetworkChannelIfName> metadata )
    {
        std::tie( mType, mChannelProtocol, mIfName ) = metadata;
    }

    /**
     * @return the Network Protocol Type supported by the consumer
     */
    inline NetworkChannelProtocol
    getChannelProtocol() const
    {
        return mChannelProtocol;
    }

protected:
    /**
     * @brief Thread safe Channel ID generator
     * @return returns a unique identifier of a channel.
     */
    NetworkChannelConsumerID
    generateChannelConsumerID()
    {
        static std::atomic<uint32_t> channelConsumerID( INVALID_CHANNEL_CONSUMER_ID );
        return ++channelConsumerID;
    }

    // The CAN Channel ID. Note this is the physical CAN Channel, not the nodeID from Cloud
    CANChannelNumericID mCANChannelID;
    CircularBufferPtr mInputBuffer;
    // shared pointer to decoder dictionary
    std::shared_ptr<const CANDecoderDictionary> mDecoderDictionaryConstPtr;
    // Signal Buffer shared pointer
    SignalBufferPtr mSignalBufferPtr;
    // Raw CAN Frame Buffer shared pointer
    CANBufferPtr mCANBufferPtr;
    NetworkChannelConsumerID mID;
    NetworkChannelType mType;
    NetworkChannelIfName mIfName;
    NetworkChannelProtocol mChannelProtocol;
};
typedef std::shared_ptr<INetworkChannelConsumer> NetworkChannelConsumerPtr;
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
