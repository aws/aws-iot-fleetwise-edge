// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "CacheAndPersist.h"
#include "ISender.h"
#include <iostream>
#include <memory>
#include <snappy.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
/**
 * @brief Namespace depending on Persistency and Connectivity
 */
namespace OffboardConnectivityAwsIot
{
using namespace Aws::IoTFleetWise::OffboardConnectivity;
using Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams;
using namespace Aws::IoTFleetWise::Platform::Linux::PersistencyManagement;

/**
 * @brief Class that handles offline data storage/retrieval and data compression before transmission
 */
class PayloadManager
{
public:
    PayloadManager( std::shared_ptr<CacheAndPersist> persistencyPtr );

    /**
     * @brief Prepares and writes the payload data to storage. Constructs and writes payload metadata JSON object.
     *
     * @param buf  buffer containing payload
     * @param size number of accessible bytes in buf
     * @param collectionSchemeParams object containing collectionScheme related metadata for data persistency and
     * transmission
     *
     * @return true if data was persisted and metadata was added, else false
     */
    bool storeData( const std::uint8_t *buf, size_t size, const struct CollectionSchemeParams &collectionSchemeParams );

    /**
     * @brief Constructs and writes payload metadata JSON object.
     *
     * @param filename file to construct metadata for
     * @param size size of the payload
     * @param collectionSchemeParams object containing collectionScheme related metadata for data persistency and
     * transmission
     */
    void storeMetadata( const std::string filename,
                        size_t size,
                        const struct CollectionSchemeParams &collectionSchemeParams );

    /**
     * @brief Retrieves metadata for all persisted files from the JSON file and removes extracted metadata from the JSON
     * file.
     *
     * @param files JSON object for all persisted files
     *
     * @return SUCCESS if metadata was successfully retrieved, FILESYSTEM_ERROR for other errors
     */
    ErrorCode retrievePayloadMetadata( Json::Value &files );

    /**
     * @brief Retrieves persisted payload from the file and deletes the file.
     *
     * @param buf  buffer containing payload
     * @param size number of accessible bytes in buf
     * @param filename filename to retrieve payload
     *
     * @return SUCCESS if metadata was successfully retrieved, FILESYSTEM_ERROR for other errors
     */
    ErrorCode retrievePayload( uint8_t *buf, size_t size, const std::string &filename );

private:
    std::shared_ptr<CacheAndPersist> mPersistencyPtr;
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws
