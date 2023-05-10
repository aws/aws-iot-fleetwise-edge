// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "DDSDataTypes.h"
#include "datatypes/VehicleDataSourceTypes.h"
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
using namespace Aws::IoTFleetWise::Platform::Linux;
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
    ~IDDSPublisher() override = default;

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
     * @return the DDS Domain ID
     */
    inline DDSDomainID
    getDDSDomainID() const
    {
        return mDDSDomainID;
    }

    /**
     * @return the DDS Topic
     */
    inline DDSTopicName
    getDDSTopic() const
    {
        return mDDSTopic;
    }

    /**
     * @return the Publisher name
     */
    inline DDSWriterName
    getDDSPublisherName() const
    {
        return mDDSwriterName;
    }

    /**
     * @return the Source Type
     */
    inline SensorSourceType
    getSensorSourceType() const
    {
        return mType;
    }

protected:
    /**
     * @brief Thread safe Channel ID generator
     * @return returns a unique identifier of a channel.
     */
    static uint32_t
    generateChannelID()
    {
        static std::atomic<uint32_t> channelID{ 0 };
        return ++channelID;
    }

    SensorSourceType mType;
    uint32_t mID;
    DDSDomainID mDDSDomainID;
    DDSTopicName mDDSTopic;
    DDSWriterName mDDSwriterName;
};
using DDSPublisherPtr = std::unique_ptr<IDDSPublisher>;
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
