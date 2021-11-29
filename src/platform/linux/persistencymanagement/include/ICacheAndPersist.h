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

#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace PersistencyManagement
{

enum ErrorCode
{
    SUCCESS = 0,
    MEMORY_FULL,
    EMPTY,
    FILESYSTEM_ERROR,
    INVALID_DATATYPE,
    INVALID_DATA
};

enum DataType
{
    EDGE_TO_CLOUD_PAYLOAD = 0,
    COLLECTION_SCHEME_LIST,
    DECODER_MANIFEST,
    DEFAULT_DATA_TYPE
};

/**
 * @brief Interface for a class that handles storage/retrieval for non-volatile memory(NVM).
 */
class ICacheAndPersist
{

public:
    /**
     * @brief destructor
     */
    virtual ~ICacheAndPersist() = 0;

    /**
     * @brief Writes to the non volatile memory(NVM).
     *
     * @param bufPtr     buffer location that contains the data to be written
     * @param size       size of the data to be written
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.

     * @return ErrorCode   SUCCESS if the write is successful,
     *                     MEMORY_FULL if the max partition size is reached,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    virtual ErrorCode write( const uint8_t *bufPtr, size_t size, DataType dataType ) = 0;

    /**
     * @brief Gets the total size of the specified data in the persistence library.
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return  total size of all the persisted data.
     */
    virtual size_t getSize( DataType dataType ) = 0;

    /**
     * @brief Reads the persisted data in a pre-allocated buffer.
     *
     * @param readBufPtr   pointer to a buffer location where data should be read
     * @param size         size to be read
     * @param dataType     specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return ErrorCode   SUCCESS if the read is successful,
     *                     EMPTY if there was no data to read
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    virtual ErrorCode read( uint8_t *const readBufPtr, size_t size, DataType dataType ) = 0;

    /**
     * @brief Deletes all the persisted data for the specified data type.
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return ErrorCode   SUCCESS if the delete is successful,
     *                     EMPTY if there was no data to erase
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    virtual ErrorCode erase( DataType dataType ) = 0;

    /**
     * @brief Returns a text representation of the error for better readable logging
     * @param err the error code to convert to string
     * @return never a nullptr, "UNKNOWN" in case of unknown string representation
     *
     */
    static const char *getErrorString( ErrorCode err );
};
} // namespace PersistencyManagement
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
