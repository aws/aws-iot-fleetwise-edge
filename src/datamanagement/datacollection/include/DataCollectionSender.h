// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "CollectionInspectionAPITypes.h"
#include "DataCollectionProtoWriter.h"
#include "ISender.h"
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::OffboardConnectivity;
using Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams;

/**
 * @brief Destination type of the data.
 *
 */
enum class SendDestination
{
    MQTT
};

/**
 * @brief Serializes collected data and sends it to the cloud.
 *        The maxMessageCount option limits the number of messages
 *        (or signals) appended to the protobuf message before the
 *        protobuf is serialized and sent to the cloud.
 */
class DataCollectionSender
{
public:
    /**
     * @brief Constructor. Setup the DataCollectionSender.
     *
     *  @param sender             ISender interface to the cloud
     *  @param maxMessageCount    Maximum number of messages before the data is serialized and sent to the cloud
     *  @param canIDTranslator    Needed to translate the internal used can channel id to the can interface id used by
     */
    DataCollectionSender( std::shared_ptr<ISender> sender,
                          unsigned maxMessageCount,
                          CANInterfaceIDTranslator &canIDTranslator );

    /**
     * @brief Serializes the collected data and transmits it to the cloud
     *
     * @param triggeredCollectionSchemeDataPtr  pointer to the collected data and metadata
     *                                 to be sent to cloud
     */
    void send( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr );

    /**
     * @brief Send the serialized data to the cloud
     *
     * @return SUCCESS if transmit was successful, else return an errorcode
     */
    ConnectivityError transmit();

    /**
     * @brief Send the serialized data to the cloud
     *
     * @param payload payload proto to be transmitted
     * @return SUCCESS if transmit was successful, else return an errorcode
     */
    ConnectivityError transmit( const std::string &payload );

private:
    uint32_t mCollectionEventID; // A unique ID that FWE generates each time a collectionScheme condition is triggered.
    std::shared_ptr<ISender> mSender;
    SendDestination mSendDestination{ SendDestination::MQTT };
    unsigned mTransmitThreshold; // max number of messages that can be sent to cloud at one time
    std::string mPersistencyPath;
    DataCollectionProtoWriter mProtoWriter;
    std::string mProtoOutput;
    CollectionSchemeParams mCollectionSchemeParams;

    /**
     * @brief Set up collectionSchemeParams struct
     */
    void setCollectionSchemeParameters( const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr );

    /**
     * @brief Serialize and send the protobuf data to the cloud
     */
    void serializeAndTransmit();
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
