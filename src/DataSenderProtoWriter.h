// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANInterfaceIDTranslator.h"
#include "CollectionInspectionAPITypes.h"
#include "OBDDataTypes.h"
#include "RawDataManager.h"
#include "TimeTypes.h"
#include "vehicle_data.pb.h"
#include <cstddef>
#include <cstdint>
#include <memory>
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
    DataSenderProtoWriter( CANInterfaceIDTranslator &canIDTranslator,
                           std::shared_ptr<RawData::BufferManager> rawDataBufferManager );

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
    void setupVehicleData( std::shared_ptr<const TriggeredCollectionSchemeData> triggeredCollectionSchemeData,
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

    //==================================================================================================================
    // NOTE: If you add new `append` functions for new types, also add them to `splitVehicleData` and `mergeVehicleData`
    //==================================================================================================================

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief Appends uploaded S3 object info to the output protobuf
     *
     * @param uploadedS3Object  Uploaded S3 object info
     */
    void append( const UploadedS3Object &uploadedS3Object );
#endif

    /**
     * @brief Sets up the DTC message info in the output protobuf
     *
     * @param msg  data and metadata for the DTC messages
     */
    void setupDTCInfo( const DTCInfo &msg );

    /**
     * @brief Gets the estimated vehicle data size in bytes
     *
     * @return the size in bytes
     */
    size_t getVehicleDataEstimatedSize() const;

    /**
     * @return True if any non-metadata is present in mVehicleData
     */
    bool isVehicleDataAdded() const;

    /**
     * @brief Serializes the vehicle data to be sent to cloud
     *
     * @param out  Protobuf
     * @return true if the data was serialized
     */

    bool serializeVehicleData( std::string *out ) const;

    /**
     * @brief Used when the serialized payload has exceeded the maximum payload size.
     * Splits the data out to a temporary instance.
     * @param data Output data
     */
    void splitVehicleData( Schemas::VehicleDataMsg::VehicleData &data );

    /**
     * @brief Used when the serialized payload has exceeded the maximum payload size.
     * Merges back the data from a temporary instance.
     * @param data Input data
     */
    void mergeVehicleData( Schemas::VehicleDataMsg::VehicleData &data );

private:
    // 2-byte overhead for LEN field, assuming strings will mostly be up to 127 bytes long. If they're larger, the size
    // of the string will anyway dominate the estimated size.
    static constexpr unsigned STRING_OVERHEAD = 2;
    Timestamp mTriggerTime;
    size_t mMetaDataEstimatedSize{};    // The estimated size in bytes of the metadata
    size_t mVehicleDataEstimatedSize{}; // The total estimated size in bytes of the mVehicleData including the metadata
    Schemas::VehicleDataMsg::VehicleData mVehicleData{};
    CANInterfaceIDTranslator mIDTranslator;
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
};

} // namespace IoTFleetWise
} // namespace Aws
