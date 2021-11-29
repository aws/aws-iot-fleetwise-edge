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
#include "DDSDataTypes.h"
#include "Listener.h"

#include "datatypes/NetworkChannelDataTypes.h"
// DDS lib related headers
#include "SensorDataListener.h"
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
using namespace eprosima::fastdds::dds;
using namespace Aws::IoTFleetWise::Platform;

/**
 * @brief Abstract DDS Subscriber Interface. Every DDS Node that wants to subscribe to data on a
 * DDS topic should implement from this interface.
 * This interface raises a notification from the SensorDataListener upon data arrival.
 */
class IDDSSubscriber : public ThreadListeners<SensorDataListener>, public DataReaderListener
{
public:
    virtual ~IDDSSubscriber() = default;

    /**
     * @brief Inits a DDS Node subscriber using the source properties.
     * @param dataSourceConfig DDS Source configuration.
     * @return True if the subscriber has been setup correctly. This includes that the domain
     * has been found and the participant has been attached the domain, and the topic has been
     * created.
     */
    virtual bool init( const DDSDataSourceConfig &dataSourceConfig ) = 0;

    /**
     * @brief Connect to the DDS Topic and start the worker thread of the subscriber.
     * @return True if the thread is active and connection has been setup.
     */
    virtual bool connect() = 0;

    /**
     * @brief Stops the thread and disconnect from the DDS Topic.
     * @return True if the thread is stopped and connection has been closed.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Checks if domain participant is alive and still connected to the DDS domain.
     * @return True if the connection is healthy.
     */
    virtual bool isAlive() = 0;

    /**
     * @return the unique ID of the channel.
     */
    inline NetworkChannelID
    getChannelID()
    {
        return mID;
    }

    /**
     * @return the Network Protocol Type
     */
    inline NetworkChannelProtocol
    getChannelProtocol()
    {
        return mNetworkProtocol;
    }

    /**
     * @return the DDS Domain ID
     */
    inline DDSDomainID
    getDDSDomainID()
    {
        return mDDSDomainID;
    }

    /**
     * @return the DDS Topic
     */
    inline DDSTopicName
    getDDSTopic()
    {
        return mDDSTopic;
    }

    /**
     * @return the Subscriber name
     */
    inline DDSWriterName
    getDDSSubscriberName()
    {
        return mDDSReaderName;
    }

    /**
     * @return the Source Type
     */
    inline SensorSourceType
    getSensorSourceType()
    {
        return mType;
    }

    /**
     * @return the DDS Topic QoS
     */
    inline DDSTopicQoS
    getDDSTopicQoS()
    {
        return mTopicQoS;
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

    SensorSourceType mType;
    NetworkChannelID mID;
    DDSDomainID mDDSDomainID;
    DDSTopicName mDDSTopic;
    DDSWriterName mDDSReaderName;
    NetworkChannelProtocol mNetworkProtocol;
    std::string mTemporaryCacheLocation;
    DDSTopicQoS mTopicQoS;
};
typedef std::unique_ptr<IDDSSubscriber> DDSSubscriberPtr;
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
