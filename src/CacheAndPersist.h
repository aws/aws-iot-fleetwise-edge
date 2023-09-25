// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <string>

// max buffer to be allocated for a read buffer
// this matches the Max Send Size on the AWS IoT channel
constexpr size_t MAX_DATA_RD_SIZE = 131072;

// file size assigned for an invalid data type
constexpr size_t INVALID_FILE_SIZE = (size_t)-1;

namespace Aws
{
namespace IoTFleetWise
{

enum class ErrorCode
{
    SUCCESS = 0,
    MEMORY_FULL,
    EMPTY,
    FILESYSTEM_ERROR,
    INVALID_DATATYPE,
    INVALID_DATA
};

enum class DataType
{
    EDGE_TO_CLOUD_PAYLOAD = 0,
    PAYLOAD_METADATA,
    COLLECTION_SCHEME_LIST,
    DECODER_MANIFEST,
    DEFAULT_DATA_TYPE
};

/**
 * @brief Class that implements the persistency interface. Handles storage/retrieval for non-volatile memory(NVM).
 *
 * Bootstrap config will specify the partition on the flash memory as well as the max partition size.
 * Underlying storage mechanism writes data to a file.
 * Multiple components using this library e.g. CollectionScheme Manager, Payload Manager are operating on its separate
 * files hence are thread safe.
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class CacheAndPersist
{

public:
    CacheAndPersist() = default;
    /**
     * @brief Constructor
     * @param partitionPath    Partition allocated for the NV storage (from config file)
     * @param maxPartitionSize Partition size should not exceed this.
     */
    CacheAndPersist( const std::string &partitionPath, size_t maxPartitionSize );

    /**
     * @brief Destructor - writes metadata from memory to the JSON file and cleans up the directory.
     */
    virtual ~CacheAndPersist();

    CacheAndPersist( const CacheAndPersist & ) = delete;
    CacheAndPersist &operator=( const CacheAndPersist & ) = delete;
    CacheAndPersist( CacheAndPersist && ) = delete;
    CacheAndPersist &operator=( CacheAndPersist && ) = delete;

    /**
     * @brief Writes to the non volatile memory(NVM) from buffer based on datatype and filename.
     *
     * @param bufPtr     buffer location that contains the data to be written
     * @param size       size of the data to be written
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     * @param filename   specifies file for the data to be written to, only valid for edge to cloud payload
     *
     * @return ErrorCode   SUCCESS if the write is successful,
     *                     MEMORY_FULL if the partition size is reached,
     *                     INVALID_DATA if the buffer ptr is NULL,
     *                     INVALID_DATATYPE if filename is empty,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    virtual ErrorCode write( const uint8_t *bufPtr,
                             size_t size,
                             DataType dataType,
                             const std::string &filename = std::string() );

    /**
     * @brief Adds new file metadata to existing JSON object.
     *
     * @param metadata   JSON object of the file metadata to persist
     */
    void addMetadata( Json::Value &metadata );

    /**
     * @brief Gets the size of data based on the datatype.
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     * @param filename   filename to get size for
     *
     * @return  size of the persisted data of specified type.
     */
    virtual size_t getSize( DataType dataType, const std::string &filename = std::string() );

    /**
     * @brief Gets the size of metadata object transformed into string stored in memory.
     *
     * @return  size of the metadata.
     */
    size_t getMetadataSize();

    /**
     * @brief Reads the persisted data of specified datatype in a pre-allocated buffer.
     *
     * @param readBufPtr   pointer to a buffer location where data should be read
     * @param size         size to be read
     * @param dataType     specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     * @param filename   specifies file for the data to read, only valid for edge to cloud payload
     *
     * @return ErrorCode   SUCCESS if the read is successful,
     *                     EMPTY if there was no data to read,
     *                     MEMORY_FULL if provided size is bigger than max size used by persistency library,
     *                     INVALID_DATATYPE if provided datatype has no associated file,
     *                     INVALID_DATA if the buffer ptr is NULL or the actual size differs from the provided,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    virtual ErrorCode read( uint8_t *const readBufPtr,
                            size_t size,
                            DataType dataType,
                            const std::string &filename = std::string() );

    /**
     * @brief Deletes persisted data for the specified data type and filename.
     * @param dataType   specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     * @param filename specifies file for the data to delete, only valid for edge to cloud payload
     *
     * @return ErrorCode   SUCCESS if the delete is successful or file does not exist,
     *                     INVALID_DATATYPE if provided datatype does not have associated filename,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode erase( DataType dataType, const std::string &filename = std::string() );

    /**
     * @brief Deletes persisted metadata.
     *
     * @return ErrorCode   SUCCESS if the delete is successful,
     *                     EMPTY if there was no data to erase,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    void clearMetadata();

    /**
     * @brief Returns a text representation of the error for better readable logging
     * @param err the error code to convert to string
     * @return never a nullptr, "UNKNOWN" in case of unknown string representation
     *
     */
    static const char *getErrorString( ErrorCode err );

    /**
     * @brief Returns metadata JSON object stored in memory
     */
    Json::Value getMetadata();

    /**
     * @brief Initializes the library by checking if the files exist and creating if necessary
     *
     * @return true if successful, else false.
     */
    bool init();

private:
    // Define File names for the components using the lib
    static constexpr const char *DECODER_MANIFEST_FILE = "DecoderManifest.bin";
    static constexpr const char *COLLECTION_SCHEME_LIST_FILE = "CollectionSchemeList.bin";
    static constexpr const char *PAYLOAD_METADATA_FILE = "PayloadMetadata.json";
    // Folder to isolate persistency workspace
    static constexpr const char *PERSISTENCY_WORKSPACE = "FWE_Persistency/";
    // Folder for payload files
    static constexpr const char *COLLECTED_DATA_FOLDER = "CollectedData/";
    // Deprecated files to clean
    static constexpr const char *DEPRECATED_COLLECTED_DATA_FILE = "CollectedData.bin";

    static constexpr const char *METADATA_SCHEME_VERSION = "1.0.0";
    // Estimate size of metadata: expected average for Proto payload is 100
    static constexpr size_t ESTIMATED_METADATA_SIZE_PER_FILE = 400;

    std::string mPersistencyPath;
    std::string mPersistencyWorkspace;
    std::string mDecoderManifestFile;
    std::string mCollectionSchemeListFile;
    std::string mPayloadMetadataFile;
    std::string mCollectedDataPath;
    std::uintmax_t mMaxPersistencePartitionSize;

    Json::Value mPersistedMetadata;

    /**
     * @brief Writes JSON object from memory to the JSON file.
     *
     * @param metadata  JSON object with new metadata to persist
     *
     * @return true if write operation succeeded
     */
    bool writeMetadata( Json::Value &metadata );

    /**
     * @brief Returns filename for the specific datatype
     *
     * @param dataType  specifies if the data is an edge to cloud payload, collectionScheme list, etc.
     *
     * @return filename defined for the datatype
     */
    std::string getFileName( DataType dataType );

    /**
     * @brief Gets the size of all persisted data, incl. decoder manifest, collection scheme, metadata, and all
     * payloads.
     *
     * @return  size of all persisted data.
     */
    std::uintmax_t getTotalSize();

    /**
     * @brief Writes to the non volatile memory(NVM) to the given path from buffer.
     *
     * @param bufPtr     buffer location that contains the data to be written
     * @param size       size of the data to be written
     * @param path   filename to write data
     *
     * @return ErrorCode   SUCCESS if the write is successful,
     *                     MEMORY_FULL if the partition size is reached,
     *                     INVALID_DATA if the buffer ptr is NULL,
     *                     INVALID_DATATYPE if filename is empty
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode write( const uint8_t *bufPtr, size_t size, std::string &path );

    /**
     * @brief Reads the persisted data from specified path in a pre-allocated buffer.
     *
     * @param readBufPtr   pointer to a buffer location where data should be read
     * @param size         size to be read
     * @param path     file to read data from
     *
     * @return ErrorCode   SUCCESS if the read is successful,
     *                     EMPTY if there was no data to read,
     *                     MEMORY_FULL if provided size is bigger than max size used by persistency library,
     *                     INVALID_DATA if the buffer ptr is NULL or the actual size differs from the provided,
     *                     INVALID_DATATYPE if filename is empty,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode read( uint8_t *const readBufPtr, size_t size, std::string &path ) const;

    /**
     * @brief Reads the persisted metadata from JSON file in a JSON object.
     *
     * @param metadata   JSON object to store persisted metadata
     *
     * @return ErrorCode   SUCCESS if the read is successful,
     *                     EMPTY if there was no data to read,
     *                     INVALID_DATA if the buffer ptr is NULL,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    ErrorCode readMetadata( Json::Value &metadata );

    /**
     * @brief Deletes specified file
     * @param path path to the file to delete.
     *
     * @return ErrorCode   SUCCESS if the delete is successful or file does not exist,
     *                     INVALID_DATATYPE if filename is empty,
     *                     FILESYSTEM_ERROR in case of any file I/O errors.
     */
    static ErrorCode erase( std::string &path );

    /**
     * @brief Gets the size of data store in the file
     * @param path path to the file where the data is stored
     *
     * @return  size of the persisted data in the file.
     */
    static size_t getSize( const std::string &path );

    /**
     * @brief Deletes all files from the persistency folder that do not have associated metadata or are not reserved for
     * collection schemes and decoder manifest.
     *
     * @return ErrorCode   SUCCESS if cleanup was completed,
     *                     FILESYSTEM_ERROR in case of any file I/O errors or if directory does not exist.
     */
    ErrorCode cleanupPersistedData();

    /**
     * @brief This function is called when component is initialised. It deletes all files, including deprecated, that
     * were written by FWE to the root persistency folder.
     *
     * @return ErrorCode   SUCCESS if cleanup was completed,
     *                     FILESYSTEM_ERROR in case of any file I/O errors or if directory does not exist.
     */
    ErrorCode cleanupDeprecatedFiles();
};

} // namespace IoTFleetWise
} // namespace Aws
