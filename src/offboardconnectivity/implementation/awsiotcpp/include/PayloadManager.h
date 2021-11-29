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
#include "CacheAndPersist.h"
#include "ISender.h"
#include "LoggingModule.h"
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
using namespace Aws::IoTFleetWise::Platform::PersistencyManagement;

#pragma pack( push, 1 )
struct PayloadHeader
{
    PayloadHeader()
        : compressionRequired( false )
        , size( 0 )
    {
    }
    bool compressionRequired;
    size_t size;
};
#pragma pack( pop )

/**
 * @brief Class that handles offline data storage/retrieval and data compression before transmission
 */
class PayloadManager
{
public:
    PayloadManager( const std::shared_ptr<CacheAndPersist> &persistencyPtr );
    ~PayloadManager();

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
    Aws::IoTFleetWise::Platform::LoggingModule mLogger;
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
    bool preparePayload( uint8_t *const buf,
                         size_t size,
                         const std::string &data,
                         const struct CollectionSchemeParams &collectionSchemeParams );
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws