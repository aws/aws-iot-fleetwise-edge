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
#include "datatypes/NetworkChannelDataTypes.h"
// DDS lib related headers
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
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
 * Metadata representing an DDS Data Request.
 * Refer to DataInspection::EventMetadata for more details
 */
struct DDSDataRequest
{
    uint32_t eventID;
    uint32_t negativeOffsetMs;
    uint32_t positiveOffsetMs;
};

/**
 * @brief Abstract DDS Publisher Interface. Every DDS Node that wants to publish data to a
 * DDS topic should implement from this interface.
 */
class IDDSPublisher : public DataWriterListener
{
public:
    virtual ~IDDSPublisher() = default;

    /**
     * @brief Inits a DDS Node publisher using the source properties.
     * @param dataSourceConfig DDS Source configuration.
     * @return True if the publisher has been setup correctly. This includes that the domain
     * has been found and the participant has been attached the domain, and the topic has been
     * created.
     */
    virtual bool init( const DDSDataSourceConfig &dataSourceConfig ) = 0;

    /**
     * @brief Connect to the DDS Topic and start the worker thread of the publisher.
     * @return True if the thread is active and connection has been setup.
     */
    virtual bool connect() = 0;

    /**
     * @brief Stops the thread and disconnect from the DDS Topic.
     * @return True if the thread is stopped and connection has been closed.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Checks if doamin participant is alive and still connected to the DDS domain.
     * @return True if the connection is healthy.
     */
    virtual bool isAlive() = 0;

    /**
     * @brief Sends the content of dataRequest over the DDS Topic.
     * This function is not thread safe, and thus as per the design, expected to be called from
     * the DDS Module thread only.
     * @return No return, this is a fire and forget
     *
     */
    virtual void publishDataRequest( const DDSDataRequest &dataRequest ) = 0;

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
     * @return the Publisher name
     */
    inline DDSWriterName
    getDDSPublisherName()
    {
        return mDDSwriterName;
    }

    /**
     * @return the Source Type
     */
    inline SensorSourceType
    getSensorSourceType()
    {
        return mType;
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
    DDSWriterName mDDSwriterName;
    NetworkChannelProtocol mNetworkProtocol;
};
typedef std::unique_ptr<IDDSPublisher> DDSPublisherPtr;
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
