// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include <string>
#include <vector>

// Default settings for the DDS Transport.
const uint32_t SEND_BUFFER_SIZE_BYTES = 0;    // Fallback to Kernel settings
const uint32_t RECEIVE_BUFFER_SIZE_BYTES = 0; // Fallback to Kernel settings
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
// DDS Types
// These type defs can be turned to the concrete ones based
// on the DDS implementation we use e.g. fast dds.
// For now, keeping them as basic C++ types.
using DDSDomainID = uint32_t;
using DDSTopicQoS = std::string;
using DDSTopicName = std::string;
using DDSReaderName = std::string;
using DDSWriterName = std::string;

/**
 * The category of the source e.g. Camera/Lidar etc. This is useful to help
 * optimize the data reading on the DDS layer and select the right IDL.
 */
enum class SensorSourceType
{
    INVALID_SENSOR_TYPE,
    CAMERA,
    RADAR,
    LIDAR
};

/**
 * The Transport protocol to be used for the DDS Communication.
 */
enum class DDSTransportType
{
    INVALID_TRANSPORT,
    SHM,
    UDP,
    TCP
};

/**
 * @brief A structure representing the configuration of a data source
 * available on a DDS Node.
 * @param sourceID Unique identifier of the source accross the system.
 * Can be the device identifier if the system has e.g. more than one camera.
 * @param sourceType The category of the source e.g. Camera/Lidar etc. This is useful to help
 * optimize the data reading on the DDS layer and select the right IDL.
 * @param domainID Identifier of the DDS domain where this source node is.
 * @param publishTopicName Name of the DDS topic used to request data from the device.
 * @param subscribeTopicName Name of the DDS topic used to receive data from the device.
 * @param topicQoS The DDS QoS to be applied on the reader and writer topics.
 * @param readerName Name of the reader on the DDS network. Useful for debugging the DDS Traffic.
 * @param writerName Name of the writer on the DDS network. Useful for debugging the DDS Traffic.
 * @param temporaryCacheLocation AWS IoT FleetWise caches the chunks of data received from the DDS
 * Network into disk locations an intermediate steps before sending them to the cloud location.
 * Each source should provide a location.
 */
struct DDSDataSourceConfig
{
    uint32_t sourceID;
    SensorSourceType sourceType;
    DDSDomainID domainID;
    DDSTopicName publishTopicName;
    DDSTopicName subscribeTopicName;
    DDSTopicQoS topicQoS;
    DDSReaderName readerName;
    DDSWriterName writerName;
    std::string temporaryCacheLocation;
    DDSTransportType transportType;
};

using DDSDataSourcesConfig = std::vector<DDSDataSourceConfig>;

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
