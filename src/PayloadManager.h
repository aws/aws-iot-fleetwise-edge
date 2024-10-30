// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CacheAndPersist.h"
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <streambuf>
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
     * @brief Write the payload data to storage together with metadata.
     *
     * @param buf buffer containing payload
     * @param size number of accessible bytes in buf
     * @param metadata metadata associated with the payload
     * @param filename full name of the file to store
     *
     * @return true if data was persisted and metadata was added, else false
     */
    virtual bool storeData( const std::uint8_t *buf,
                            size_t size,
                            const Json::Value &metadata,
                            const std::string &filename );

    /**
     * @brief Calls CacheAndPersist module to write the given stream to file
     *
     * @param streambuf stream with data to be persisted
     * @param metadata metadata associated with the payload
     * @param filename full name of the file to store
     *
     * @return true if data was persisted and metadata was added, else false
     */
    virtual bool storeData( std::streambuf &streambuf, const Json::Value &metadata, const std::string &filename );

    /**
     * @brief Store only metadata for a file. This can be used when sending a file failed and we want to keep persisting
     * it.
     *
     * @param metadata metadata associated with the payload
     */
    virtual void storeMetadata( const Json::Value &metadata );

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

    /**
     * @brief Retrieves persisted payload from the file as a stream to be read on demand.
     *
     * @param fileStream   the file stream that will point to the file being requested
     * @param filename filename to retrieve payload
     *
     * @return SUCCESS if metadata was successfully retrieved, FILESYSTEM_ERROR for other errors
     */
    virtual ErrorCode retrievePayloadLazily( std::ifstream &fileStream, const std::string &filename );

    /**
     * @brief Delete a payload file. Only a the payload file is deleted, not the medadata.
     *
     * This normally should be called after retrievePayloadLazily.
     *
     * @param filename
     */
    virtual void deletePayload( const std::string &filename );

private:
    std::shared_ptr<CacheAndPersist> mPersistencyPtr;
};

} // namespace IoTFleetWise
} // namespace Aws
