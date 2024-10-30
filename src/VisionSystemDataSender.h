// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "DataSenderIonWriter.h"
#include "DataSenderTypes.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "S3Sender.h"
#include <cstdint>
#include <istream>
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
    VisionSystemDataSender( std::shared_ptr<DataSenderQueue> uploadedS3Objects,
                            std::shared_ptr<S3Sender> s3Sender,
                            std::shared_ptr<DataSenderIonWriter> ionWriter,
                            std::string vehicleName );

    ~VisionSystemDataSender() override = default;

    void processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback ) override;

    void processPersistedData( std::istream &data,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

    virtual void onChangeCollectionSchemeList(
        const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes );

private:
    std::shared_ptr<DataSenderQueue> mUploadedS3Objects;
    std::shared_ptr<DataSenderIonWriter> mIonWriter;
    std::shared_ptr<S3Sender> mS3Sender; // might be nullptr
    std::string mVehicleName;
    std::mutex mActiveCollectionSchemeMutex;
    std::shared_ptr<const ActiveCollectionSchemes> mActiveCollectionSchemes;

    /**
     * @brief Put vision system data into ion in chunks. Initiate serialization, compression, and
     * upload for each partition.
     * @param triggeredVisionSystemData collected data
     * @param callback callback function to be called after data is processed
     */
    void transformVisionSystemDataToIon( std::shared_ptr<const TriggeredVisionSystemData> triggeredVisionSystemData,
                                         OnDataProcessedCallback callback );

    S3UploadMetadata getS3UploadMetadataForCollectionScheme( const std::string &collectionSchemeID );
};

} // namespace IoTFleetWise
} // namespace Aws
