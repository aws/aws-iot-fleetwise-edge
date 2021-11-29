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
#include "DataCollectionJSONWriter.h"
#include "DataCollectionProtoWriter.h"
#include "INetworkChannelConsumer.h"
#include "ISender.h"
#include "LoggingModule.h"
#include "OBDOverCANModule.h"
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform;
using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::OffboardConnectivity;
using Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams;

/**
 * @brief Serializes collected data and sends it to the cloud.
 *        Optionally supports debug JSON output of collected data.
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
     *  @param jsonOutputEnabled  True to enable debug JSON output of the collected data
     *  @param maxMessageCount    Maximum number of messages before the data is serialized and sent to the cloud
     *  @param canIDTranslator    Needed to translate the internal used can channel id to the can interface id used by
     * cloud
     */
    DataCollectionSender( std::shared_ptr<ISender> sender,
                          bool jsonOutputEnabled,
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
    LoggingModule mLogger;
    uint32_t mCollectionEventID; // A unique ID that FWE generates each time a collectionScheme condition is triggered.
    std::shared_ptr<ISender> mSender;
    bool mJsonOutputEnabled;
    unsigned mTransmitThreshold; // max number of messages that can be sent to cloud at one time
    DataCollectionProtoWriter mProtoWriter;
    DataCollectionJSONWriter mJsonWriter;
    std::string mProtoOutput;
    CollectionSchemeParams mCollectionSchemeParams;

    /**
     * @brief Set up collectionSchemeParams struct
     */
    void setCollectionSchemeParameters( const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr );

    /**
     * @brief Get the collection event ID for the data to be serialized
     *
     * @return collection event ID
     */

    uint32_t getCollectionEventId() const;

    /**
     * @brief Serialize and send the protobuf data to the cloud
     */
    void serializeAndTransmit();
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
