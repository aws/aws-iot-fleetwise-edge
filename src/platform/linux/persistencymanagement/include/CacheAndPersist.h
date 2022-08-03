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
#include "ICacheAndPersist.h"
#include "LoggingModule.h"
#include <map>
#include <string>
#include <vector>

// max buffer to be allocated for a read buffer
// this matches the Max Send Size on the AWS IoT channel
constexpr size_t MAX_DATA_RD_SIZE = 131072;

// file size assigned for an invalid data type
constexpr size_t INVALID_FILE_SIZE = (size_t)-1;

// Define File names for the components using the lib
#define DECODER_MANIFEST_FILE "/DecoderManifest.bin"
#define COLLECTION_SCHEME_LIST_FILE "/CollectionSchemeList.bin"
#define COLLECTED_DATA_FILE "/CollectedData.bin"

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{
namespace PersistencyManagement
{
/**
 * @brief Class that implements the persistency interface. Handles storage/retrieval for non-volatile memory(NVM).
 *
 * Bootstrap config will specify the partition on the flash memory as well as the max partition size.
 * Underlying storage mechanism writes data to a file.
 * Multiple components using this library e.g. CollectionScheme Manager, Payload Manager are operating on its separate
 * files hence are thread safe.
 */
class CacheAndPersist : public ICacheAndPersist
{

public:
    /**
     * @brief Constructor
     * @param partitionPath    Partition allocated for the NV storage (from config file)
     * @param maxPartitionSize Partition size should not exceed this.
     */
    CacheAndPersist( const std::string &partitionPath, size_t maxPartitionSize );

    /**
     * @brief Writes to the non volatile memory(NVM).
     *
     * @param bufPtr     buffer location that contains the data to be written
     * @param size       size of the data to be written
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return ErrorCode   SUCCESS if the write is successful,
     *                     MEMORY_FULL if the partition size is reached,
     *                     INVALID_DATA if the buffer ptr is NULL
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode write( const uint8_t *bufPtr, size_t size, DataType dataType ) override;

    /**
     * @brief Gets the total size of data in the persistence library.
     *        Includes data written to the file as well as any data in flight i.e. in cache.
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return  total size of all the persisted data.
     */
    size_t getSize( DataType dataType ) override;

    /**
     * @brief Reads the persisted data in a pre-allocated buffer.
     *
     * @param readBufPtr   pointer to a buffer location where data should be read
     * @param size         size to be read
     * @param dataType     specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return ErrorCode   SUCCESS if the read is successful,
     *                     EMPTY if there was no data to read
     *                     INVALID_DATA if the buffer ptr is NULL
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode read( uint8_t *const readBufPtr, size_t size, DataType dataType ) override;

    /**
     * @brief Deletes all the persisted data for the specified data type.
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return ErrorCode   SUCCESS if the delete is successful,
     *                     EMPTY if there was no data to erase
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode erase( DataType dataType ) override;

    /**
     * @brief Initializes the library by checking if the files exist and creating if necessary
     *
     * @return true if successful, else false.
     */
    bool init();

private:
    std::string mDecoderManifestFile;
    std::string mCollectionSchemeListFile;
    std::string mCollectedDataFile;
    size_t mMaxPersistencePartitionSize;
    LoggingModule mLogger;

    /**
     * @brief checks if the specified file exists, if not creates a new one
     *
     * @param fileName  Absolute file path along with the filename to be created
     * @return SUCCESS if the file is created, FILESYSTEM_ERROR if not.
     */
    static ErrorCode createFile( const std::string &fileName );
};
} // namespace PersistencyManagement
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
