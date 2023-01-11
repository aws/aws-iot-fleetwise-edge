// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "Listener.h"
#include "VehicleDataSourceListener.h"
#include "datatypes/VehicleDataMessage.h"
#include "datatypes/VehicleDataSourceConfig.h"
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
using namespace Aws::IoTFleetWise::Platform::Linux;
// Single Producer/Consumer buffer. Used for data processing between the source and the consumer.
using VehicleMessageCircularBuffer = boost::lockfree::spsc_queue<VehicleDataMessage>;
using VehicleMessageCircularBufferPtr = std::shared_ptr<VehicleMessageCircularBuffer>;
/**
 * @brief Abstract Interface for a Vehicle Data Source. A data source maps to exactly one Transport
 * Connector implementation. The Transport protocol details are abstracted away so that users of
 * the data sources only act on the output of the source i.e. the Circular buffer.
 * This interface does not mandate any threading model on implementers but all APIs are expected to
 * be thread safe.
 */
class AbstractVehicleDataSource : public ThreadListeners<VehicleDataSourceListener>
{
public:
    ~AbstractVehicleDataSource() override = default;

    /**
     * @brief Initializes the Vehicle Data Source.
     * @param sourceConfigs The source configuration. One source can handle
     * multiple configs at a time if composition is wanted e.g. multiple
     * raw network  sources consolidated into one outbound vehicle data source.
     * Configurations used to init the source must be of the same RAW Network protocol type.
     * @return True if the Init succeeds.
     */
    virtual bool init( const std::vector<VehicleDataSourceConfig> &sourceConfigs ) = 0;

    /**
     * @brief Connect the Data Source to the underlying Transport.
     * @return True if the connection has been setup.
     */
    virtual bool connect() = 0;

    /**
     * @brief Connect the Data Source to the underlying Transport.
     * @return True if connection has been closed.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Checks if the connection to the underlying Transport and data
     * is being consumed,
     *  Implementation varies among Transports.
     * @return True if the connection is healthy.
     */
    virtual bool isAlive() = 0;

    /**
     * @brief Ask the source to suspend the data acquisition from the Transport.
     */
    virtual void suspendDataAcquisition() = 0;

    /**
     * @brief Ask the source to resume the data acquisition from the Transport.
     */
    virtual void resumeDataAcquisition() = 0;

    /**
     * @brief Handle of the Vehicle Data Source circular buffer. User of the Data Source
     * can use this object to consume data. The buffer can ONLY be consume
     * from one single consumer thread.
     * @return shared object pointer to the circular buffer.
     */
    inline VehicleMessageCircularBufferPtr
    getBuffer()
    {
        return mCircularBuffPtr;
    }
    /**
     * @brief Provide the Transport Protocol Type used by the source.
     * If multiple source configs are provided, they are assumed have the same
     * Transport protocol.
     * @return returns the transport type.
     */
    inline VehicleDataSourceProtocol
    getVehicleDataSourceProtocol()
    {
        return mNetworkProtocol;
    }
    /**
     * @return the unique ID of the Source.
     */
    inline VehicleDataSourceID
    getVehicleDataSourceID() const
    {
        return mID;
    }
    /**
     * @return the data source type.
     */
    inline VehicleDataSourceType
    getVehicleDataSourceType()
    {
        return mType;
    }

    /**
     * @return the network interface name. In case multiple
     * network interfaces ( e.g. multiple CAN IFs ) are aggregated in one source,
     * only one IF Name will be returned.
     */
    inline VehicleDataSourceIfName
    getVehicleDataSourceIfName()
    {
        return mIfName;
    }

protected:
    /**
     * @brief Thread safe Source ID generator
     * @return returns a unique identifier of a source.
     */
    static VehicleDataSourceID
    generateSourceID()
    {
        static std::atomic<VehicleDataSourceID> sourceID( INVALID_DATA_SOURCE_ID );
        return ++sourceID;
    }
    // A FIFO queue holding the currently acquired vehicle data messages.
    VehicleMessageCircularBufferPtr mCircularBuffPtr;
    // Current active Data Source Configurations
    std::vector<VehicleDataSourceConfig> mConfigs;
    // Unique Identifier of the source.
    VehicleDataSourceID mID;
    // A data source has one single Transport even if we are dealing with
    // multiple raw data sources at the network level.
    VehicleDataSourceProtocol mNetworkProtocol;
    // The data source network interface name.
    VehicleDataSourceIfName mIfName;
    // Type of the source
    VehicleDataSourceType mType;
};
using VehicleDataSourcePtr = std::shared_ptr<AbstractVehicleDataSource>;
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
