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

#pragma pack( push, 1 )
struct PayloadHeader
{
    bool compressionRequired{ false };
    size_t size{ 0 };
};

#pragma pack( pop )

/**
 * @brief Class that handles offline data storage/retrieval and data compression before transmission
 */
class PayloadManager
{
public:
    PayloadManager( std::shared_ptr<CacheAndPersist> persistencyPtr );

    /**
     * @brief Prepare the payload data to be written to storage. Adds a header with metadata consisting
     *        of compression flag and size of the payload.
     *
     * @param buf  buffer containing payload
     * @param size number of accessible bytes in buf
     * @param collectionSchemeParams object containing collectionScheme related metadata for data persistency and
     * transmission
     *
     * @return true if data was persisted, else false
     */
    bool storeData( const std::uint8_t *buf, size_t size, const struct CollectionSchemeParams &collectionSchemeParams );

    /**
     * @brief Parses the retrieved data from the storage. Separates metadata from the actual payload.
     *
     * @param data  vector to store parsed payloads
     *
     * @return SUCCESS if true, EMPTY if no data to retrieve, FILESYSTEM_ERROR if other errors
     */
    ErrorCode retrieveData( std::vector<std::string> &data );

private:
    std::shared_ptr<CacheAndPersist> mPersistencyPtr;

    /**
     * @brief Prepare the payload data to be written to storage. Adds a header with metadata consisting
     *        of compression flag and size of the payload.
     *
     * @param buf  buffer to store encoded payload
     * @param data proto data to be stored
     * @param size size of the buffer being passed
     * @param collectionSchemeParams object containing collectionScheme related metadata for data persistency and
     * transmission
     *
     * @return true if prep was successful, false if error occurred
     */
    static bool preparePayload( uint8_t *const buf,
                                size_t size,
                                const std::string &data,
                                const CollectionSchemeParams &collectionSchemeParams );
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws
