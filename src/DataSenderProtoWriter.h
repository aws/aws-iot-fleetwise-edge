// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANInterfaceIDTranslator.h"
#include "CollectionInspectionAPITypes.h"
#include "GeohashInfo.h"
#include "OBDDataTypes.h"
#include "TimeTypes.h"
#include "vehicle_data.pb.h"
#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Class that does the protobuf setup for the collected data
 *        and serializes the edge to cloud data
 */
class DataSenderProtoWriter
{
public:
    /**
     * @brief Constructor. Setup the DataSenderProtoWriter.
     */
    DataSenderProtoWriter( CANInterfaceIDTranslator &canIDTranslator );

    /**
     * @brief Destructor.
     */
    ~DataSenderProtoWriter();

    DataSenderProtoWriter( const DataSenderProtoWriter & ) = delete;
    DataSenderProtoWriter &operator=( const DataSenderProtoWriter & ) = delete;
    DataSenderProtoWriter( DataSenderProtoWriter && ) = delete;
    DataSenderProtoWriter &operator=( DataSenderProtoWriter && ) = delete;

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
    Schemas::VehicleDataMsg::VehicleData mVehicleData{};
    CANInterfaceIDTranslator mIDTranslator;
};

} // namespace IoTFleetWise
} // namespace Aws
