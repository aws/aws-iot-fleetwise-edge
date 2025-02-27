// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataSenderIonWriter.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/S3Sender.h"
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

constexpr char DEFAULT_KEY_SUFFIX[] = ".10n"; // Ion is the only supported format

/**
 * @brief Struct that specifies the persistence and transmission attributes
 *        for the S3 upload
 */
struct S3UploadParams
{
    std::string region;        // bucket region, set on the campaign level, attribute of S3 client
    std::string bucketName;    // bucket name, set on the campaign level, attribute of S3 request
    std::string bucketOwner;   // bucket owner account ID, set on the campaign level, attribute of S3 request
    std::string objectName;    // object key, attribute of S3 request
    std::string uploadID;      // upload ID of the multipart upload
    uint16_t multipartID{ 0 }; // multipartID of a single part of the multipart upload, allowed values are 1 to 10000

public:
    bool
    operator==( const S3UploadParams &other ) const
    {
        return ( bucketName == other.bucketName ) && ( bucketOwner == other.bucketOwner ) &&
               ( objectName == other.objectName ) && ( region == other.region ) && ( uploadID == other.uploadID ) &&
               ( multipartID == other.multipartID );
    }

    bool
    operator!=( const S3UploadParams &other ) const
    {
        return !( *this == other );
    }
};

class VisionSystemDataSender : public DataSender
{

public:
    VisionSystemDataSender( DataSenderQueue &uploadedS3Objects,
                            S3Sender &s3Sender,
                            std::unique_ptr<DataSenderIonWriter> ionWriter,
                            std::string vehicleName );

    ~VisionSystemDataSender() override = default;

    bool isAlive() override;

    void processData( const DataToSend &data, OnDataProcessedCallback callback ) override;

    void processPersistedData( const uint8_t *buf,
                               size_t size,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

    virtual void onChangeCollectionSchemeList( std::shared_ptr<const ActiveCollectionSchemes> activeCollectionSchemes );

private:
    DataSenderQueue &mUploadedS3Objects;
    std::unique_ptr<DataSenderIonWriter> mIonWriter;
    S3Sender &mS3Sender;
    std::string mVehicleName;
    std::mutex mActiveCollectionSchemeMutex;
    std::shared_ptr<const ActiveCollectionSchemes> mActiveCollectionSchemes;

    /**
     * @brief Put vision system data into ion in chunks. Initiate serialization, compression, and
     * upload for each partition.
     * @param triggeredVisionSystemData collected data
     * @param callback callback function to be called after data is processed
     */
    void transformVisionSystemDataToIon( const TriggeredVisionSystemData &triggeredVisionSystemData,
                                         OnDataProcessedCallback callback );

    S3UploadMetadata getS3UploadMetadataForCollectionScheme( const std::string &collectionSchemeID );
};

} // namespace IoTFleetWise
} // namespace Aws
