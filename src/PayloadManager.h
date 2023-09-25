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

namespace Aws
{
namespace IoTFleetWise
{

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
    );

    /**
     * @brief Constructs and writes payload metadata JSON object.
     */
    virtual void storeMetadata(
        const std::string filename, /**< filename file to construct metadata for */
        size_t size,                /**< size of the payload */
        const CollectionSchemeParams
            &collectionSchemeParams /**< collectionSchemeParams object containing
                                         collectionScheme related metadata for data persistency and transmission */
    );

    /**
     * @brief Retrieves metadata for all persisted files from the JSON file and removes extracted metadata from the JSON
     * file.
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
