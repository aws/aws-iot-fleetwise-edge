// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICollectionScheme.h"
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
using ICollectionSchemePtr = std::shared_ptr<ICollectionScheme>;

/**
 * @brief ICollectionSchemeList is used to exchange a list of collection collectionSchemes from
 * Schema to CollectionSchemeManagement.
 *
 * This list of collection collectionSchemes will arrive as a binary proto of type
 * collectionSchemes.proto in a callback function running in the context of an Aws IoT
 * MQTT callback. Schema will lazily pack this this binary proto into
 * this class, and expose a getter function which will lightly de-serialize the
 * proto and return an array of ICollectionSchemePtr.
 *
 * The reason this abstract interface exists is because we do not want to do any
 * de-serialization in Aws IoT Core MQTT callbacks.
 */
class ICollectionSchemeList
{
public:
    /**
     * @brief Parses the protobuffer data and builds a vector of validated CollectionSchemes. These
     * collectionSchemes are validated
     *
     * @return True if success, false if failure. On failure, no collectionSchemes inside will be usable
     */
    virtual bool build() = 0;

    /**
     * @brief Checks if the data inside this message has been parsed using the build function.
     *
     * @return True if data is ready to be read, false if otherwise. If this is false after the message has been built,
     * it means the data is corrupted and this message cannot be used
     */
    virtual bool isReady() const = 0;

    /**
     * @brief De-serialize the collectionSchemes.proto proto and returns an array of ICollectionSchemePtrs contained in
     * the underlying collectionSchemes.proto object.
     *
     * @return An array of built ICollectionSchemePtrs ready to be used CollectionSchemeManagement. Returns an empty
     * array in case of an error.
     */
    virtual const std::vector<ICollectionSchemePtr> &getCollectionSchemes() const = 0;

    /**
     * @brief Used by the AWS IoT MQTT callback to copy data received from Cloud into this object without any further
     * processing to minimize time spent in callback context.
     *
     * @param inputBuffer Byte array of raw protobuffer data for a collectionSchemes.proto type binary blob
     * @param size Size of the data buffer
     *
     * @return True if successfully copied, false if failure to copy data.
     */
    virtual bool copyData( const std::uint8_t *inputBuffer, const size_t size ) = 0;

    /**
     * @brief This function returns mProtoBinaryData majorly used for persistent
     * storage
     *
     * @return binary data in a vector
     */
    virtual const std::vector<uint8_t> &getData() const = 0;

    virtual ~ICollectionSchemeList() = default;
};

} // namespace IoTFleetWise
} // namespace Aws
