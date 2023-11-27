// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CacheAndPersist.h"
#include "ISender.h"
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include <streambuf>
#endif

namespace Aws
{
namespace IoTFleetWise
{

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
/**
 * @brief Struct that specifies the persistence and transmission attributes
 *        for the S3 upload
 */
struct S3UploadParams
{
    std::string region{ "" };      // bucket region, set on the campaign level, attribute of S3 client
    std::string bucketName{ "" };  // bucket name, set on the campaign level, attribute of S3 request
    std::string bucketOwner{ "" }; // bucket owner account ID, set on the campaign level, attribute of S3 request
    std::string objectName{ "" };  // object key, attribute of S3 request
    std::string uploadID{ "" };    // upload ID of the multipart upload
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
#endif

/**
 * @brief Class that handles offline data storage/retrieval and data compression before transmission
 */
class PayloadManager
{
public:
    PayloadManager( std::shared_ptr<CacheAndPersist> persistencyPtr );

    virtual ~PayloadManager() = default;

    /**
     * @brief Prepares and writes the payload data to storage. Constructs and writes payload metadata JSON object.
     *
     * @return true if data was persisted and metadata was added, else false
     */
    virtual bool storeData(
        const std::uint8_t *buf,                             /**< buffer containing payload */
        size_t size,                                         /**< size number of accessible bytes in buf */
        const CollectionSchemeParams &collectionSchemeParams /**< object containing collectionScheme related
                                                                       metadata for data persistency and transmission */
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        ,
        const struct S3UploadParams &s3UploadParams =
            S3UploadParams() /**< object containing metadata related to the S3 upload for data persistency and
                                transmission. If object is not passed or contains default values, no S3 related
                                parameters are written to the JSON metadata file. */
#endif
    );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief Calls CacheAndPersist module to write the Ion file
     *
     * @param streambuf  stream with the Ion data
     * @param filename full name of the file to store
     *
     * @return true if data was persisted and metadata was added, else false
     */
    virtual bool storeIonData( std::unique_ptr<std::streambuf> streambuf, std::string filename );
#endif

    /**
     * @brief Constructs and writes payload metadata JSON object.
     */
    virtual void storeMetadata(
        const std::string filename, /**< filename file to construct metadata for */
        size_t size,                /**< size of the payload */
        const CollectionSchemeParams
            &collectionSchemeParams /**< collectionSchemeParams object containing collectionScheme
                                       related metadata for data persistency and transmission */
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        ,
        const struct S3UploadParams &s3UploadParams =
            S3UploadParams() /**< object containing metadata related to the S3 upload for data persistency and
                                transmission. If object is not passed or contains default values, no S3 related
                                parameters are written to the JSON metadata file. */
#endif
    );

    /**
     * @brief Retrieves metadata for all persisted files from the JSON file and removes extracted metadata from the
     * JSON file.
     *
     * @param files JSON object for all persisted files
     *
     * @return SUCCESS if metadata was successfully retrieved, FILESYSTEM_ERROR for other errors
     */
    virtual ErrorCode retrievePayloadMetadata( Json::Value &files );

    /**
     * @brief Retrieves persisted payload from the file and deletes the file.
     *
     * @param buf  buffer containing payload
     * @param size number of accessible bytes in buf
     * @param filename filename to retrieve payload
     *
     * @return SUCCESS if metadata was successfully retrieved, FILESYSTEM_ERROR for other errors
     */
    virtual ErrorCode retrievePayload( uint8_t *buf, size_t size, const std::string &filename );

private:
    std::shared_ptr<CacheAndPersist> mPersistencyPtr;
    std::mutex mMetadataMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
