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
#include "CANInterfaceIDTranslator.h"
#include "CollectionInspectionAPITypes.h"
#include "OBDDataTypes.h"
#include "vehicle_data.pb.h"
#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Schemas;
using namespace Aws::IoTFleetWise::DataInspection;

/**
 * @brief Class that does the protobuf setup for the collected data
 *        and serializes the edge to cloud data
 */
class DataCollectionProtoWriter
{
public:
    /**
     * @brief Constructor. Setup the DataCollectionProtoWriter.
     */
    DataCollectionProtoWriter( CANInterfaceIDTranslator &canIDTranslator );

    /**
     * @brief Destructor.
     */
    ~DataCollectionProtoWriter();

    DataCollectionProtoWriter( const DataCollectionProtoWriter & ) = delete;
    DataCollectionProtoWriter &operator=( const DataCollectionProtoWriter & ) = delete;
    DataCollectionProtoWriter( DataCollectionProtoWriter && ) = delete;
    DataCollectionProtoWriter &operator=( DataCollectionProtoWriter && ) = delete;

    /**
     * @brief Does the protobuf set up for the data collected by inspection engine
     *
     * @param triggeredCollectionSchemeData     pointer to the collected data and metadata
     *                                 to be sent to cloud
     * @param collectionEventID       a unique ID to tie multiple signals to a single collection event
     */
    void setupVehicleData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData,
                           uint32_t collectionEventID );

    /**
     * @brief Appends the decoded CAN/OBD signal messages to the output protobuf
     *
     *  @param msg  data and metadata for the captured signal
     */
    void append( const CollectedSignal &msg );

    /**
     * @brief Appends the raw CAN frame messages to the output protobuf
     *
     *  @param msg  data and metadata for the raw CAN frames
     */
    void append( const CollectedCanRawFrame &msg );

    /**
     * @brief Appends the DTC codes to the output protobuf
     *
     * @param dtc  diagnostic trouble codes
     */
    void append( const std::string &dtc );

    /**
     * @brief Appends the Geohash String to the output protobuf
     *
     * @param geohashInfo  Geohash information
     */
    void append( const GeohashInfo &geohashInfo );

    /**
     * @brief Sets up the DTC message info in the output protobuf
     *
     * @param msg  data and metadata for the DTC messages
     */
    void setupDTCInfo( const DTCInfo &msg );

    /**
     * @brief Gets the total number of collectionScheme messages sent to the cloud
     *
     * @return the total number of collectionScheme messages added to the edge to cloud payload
     */
    unsigned getVehicleDataMsgCount() const;

    /**
     * @brief Serializes the vehicle data to be sent to cloud
     *
     * @param out  Protobuf
     * @return true if the data was serialized
     */

    bool serializeVehicleData( std::string *out ) const;

private:
    Timestamp mTriggerTime;
    unsigned mVehicleDataMsgCount{}; // tracks the number of messages being sent in the edge to cloud payload
    VehicleDataMsg::VehicleData mVehicleData{};
    CANInterfaceIDTranslator mIDTranslator;
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
