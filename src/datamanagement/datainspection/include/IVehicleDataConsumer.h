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

#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "businterfaces/AbstractVehicleDataSource.h"
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
using VehicleDataConsumerID = uint32_t;
constexpr VehicleDataConsumerID INVALID_DATA_CONSUMER_ID = 0;

/**
 * @brief Abstract Vehicle Data Consumer Interface. A Vehicle Data Consumer is
 * bound to only one Vehicle Data Source. It receives the raw or synthetic data from
 * the Vehicle Data Source and decodes/filters the data according to the decoder dictionary.
 * The result will be put into three output buffers for inspection engine to process in a fanout
 * fashion.
 * The life cycle of this Consumer is fully managed by the VehicleDataSourceBinder worker thread.
 * Only instantiation is allowed on startup.
 */
class IVehicleDataConsumer
{
public:
    IVehicleDataConsumer() = default;
    virtual ~IVehicleDataConsumer() = default;

    /**
     * @brief Initializes the Vehicle Data Consumer.
     * @param dataSourceID Vehicle Data Source ID
     * @param signalBufferPtr Signal Buffer shared pointer.
     * @param idleTimeMs if no new data is available sleep for this amount of milliseconds
     * @return True if the Consumer has been setup correctly.
     */
    virtual bool init( VehicleDataSourceID dataSourceID, SignalBufferPtr signalBufferPtr, uint32_t idleTimeMs ) = 0;

    /**
     * @brief Creates and starts the worker thread of the consumer to start
     * consuming the data from the input buffer.
     * @return True if the thread is active and consumption has started.
     */
    virtual bool connect() = 0;

    /**
     * @brief Stops the thread and disconnect from the Vehicle Data Source input buffer.
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
     * and Vehicle Data Consumer. This buffer holds the transformed data from its raw form into
     * a synthetic representation i.e. after applying the decoding rules.
     * @return shared object pointer to the Signal buffer.
     */
    inline SignalBufferPtr
    getSignalBufferPtr() const
    {
        return mSignalBufferPtr;
    }

    /**
     * @brief Set the consumption buffer of the consumer.
     * @param producerBufferPtr Pointer to the Producer
     */
    inline void
    setInputBuffer( VehicleMessageCircularBufferPtr producerBufferPtr )
    {
        mInputBufferPtr = std::move( producerBufferPtr );
    }

    /**
     * @return the unique ID of the consumer.
     */
    inline VehicleDataConsumerID
    getConsumerID() const
    {
        return mID;
    }

    /**
     * @brief Set the consumption buffer of the consumer.
     */
    inline void
    setChannelMetadata( std::tuple<VehicleDataSourceType, VehicleDataSourceProtocol, VehicleDataSourceIfName> metadata )
    {
        std::tie( mType, mDataSourceProtocol, mIfName ) = metadata;
    }

    /**
     * @return the Network Protocol Type supported by the consumer
     */
    inline VehicleDataSourceProtocol
    getVehicleDataSourceProtocol() const
    {
        return mDataSourceProtocol;
    }

protected:
    /**
     * @brief Thread safe Consumer ID generator
     * @return returns a unique identifier of a consumer ID.
     */
    static VehicleDataConsumerID
    generateConsumerID()
    {
        static std::atomic<uint32_t> consumerID( INVALID_DATA_CONSUMER_ID );
        return ++consumerID;
    }

    VehicleDataSourceID mDataSourceID;
    // Input Buffer on which the consumer will run the decoding rules.
    VehicleMessageCircularBufferPtr mInputBufferPtr;
    // shared pointer to decoder dictionary
    std::shared_ptr<const DecoderDictionary> mDecoderDictionaryConstPtr;
    // Signal Buffer shared pointer
    SignalBufferPtr mSignalBufferPtr;
    VehicleDataConsumerID mID;
    VehicleDataSourceType mType;
    VehicleDataSourceIfName mIfName;
    VehicleDataSourceProtocol mDataSourceProtocol;
};
using VehicleDataConsumerPtr = std::shared_ptr<IVehicleDataConsumer>;
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
