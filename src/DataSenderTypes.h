// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "QueueTypes.h"
#include <boost/variant.hpp>
#include <json/json.h>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Defines all available types of data that can be sent
 *
 * Usually each type will map to a specific implementation of DataSender, which knowns how to handle
 * and send that specific type.
 */
enum class SenderDataType
{
    TELEMETRY = 0,
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    VISION_SYSTEM = 1,
#endif
};

/**
 * @brief Represents a piece of data that can be sent
 *
 * The concrete type can contain any kind of data, but DataSenderManager will only
 * know about this abstract type and route it to the corresponding DataSender, which is expected to
 * cast it to the expected concrete type.
 */
struct DataToSend
{
    virtual ~DataToSend() = default;

    virtual SenderDataType getDataType() const = 0;
};

/**
 * @brief Represents a piece of data that should be persisted (most likely because sending it failed)
 */
struct DataToPersist
{
    virtual ~DataToPersist() = default;

    virtual SenderDataType getDataType() const = 0;

    /**
     * @brief Provide the metadata to associated with this data when persisted
     *
     * @return A Json::Value object containing any amount of properties
     */
    virtual Json::Value getMetadata() const = 0;

    /**
     * @brief Provide the filename that to associated with this data when persisted
     *
     * @return The relative filename which will contain the data. It should contain enough details to make it unique.
     */
    virtual std::string getFilename() const = 0;

    /**
     * @brief Provide the underlying payload data
     *
     * The return type is a variant to allow implementations to choose the most appropriate type, more specifically to
     * allow cases where the payload is fully in memory and cases where the payload is generated on demand.
     * In any case DataSenderManager will handle any of the types and do whatever is necessary to persist the data.
     *
     * @return The payload data in one of the variant types.
     */
    virtual boost::variant<std::shared_ptr<std::string>, std::shared_ptr<std::streambuf>> getData() const = 0;
};

/**
 * @brief Map the SenderDataType enum to a string
 *
 * This is mostly to be used when serializing the data (e.g. in a JSON file).
 * The result can be mapped back to the enum with stringToSenderDataType().
 *
 * @param dataType The SenderDataType to map
 * @return The string representation of the SenderDataType
 */
inline std::string
senderDataTypeToString( SenderDataType dataType )
{
    switch ( dataType )
    {
    case SenderDataType::TELEMETRY:
        return "Telemetry";
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    case SenderDataType::VISION_SYSTEM:
        return "VisionSystem";
#endif
    default:
        return "";
    }
}

/**
 * @brief Map a string to a SenderDataType enum
 *
 * This is mostly to be used when deserializing the data (e.g. from a JSON file).
 * The result can be mapped back to the string with senderDataTypeToString().
 *
 * @param dataType The string representation of the SenderDataType
 * @param output The reference that will contain the result if successful.
 * @return Whether the conversion succeeded. If false, output will be unmodified.
 */
inline bool
stringToSenderDataType( const std::string &dataType, SenderDataType &output )
{
    if ( dataType == "Telemetry" )
    {
        output = SenderDataType::TELEMETRY;
        return true;
    }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    else if ( dataType == "VisionSystem" )
    {
        output = SenderDataType::VISION_SYSTEM;
        return true;
    }
#endif

    return false;
}

/**
 * @brief Callback to be called when a data sender finished processing the data
 *
 * @param success true if the data was successfully processed, false otherwise
 * @param data in case of failure this represents the data that needs to be persisted. It can be nullptr, which means
 * that nothing should be persisted.
 */
using OnDataProcessedCallback = std::function<void( bool success, std::shared_ptr<const DataToPersist> data )>;

/**
 * @brief Callback to be called when a data sender finished processing a data that has been persisted
 *
 * @param success true if the data was successfully processed, false otherwise
 */
using OnPersistedDataProcessedCallback = std::function<void( bool success )>;

/**
 * @brief A sender interface to be used by DataSenderManager
 *
 * Each implementation knowns how to handle a specific type (or multiple) and is expected to cast the
 * data to what is expected.
 */
class DataSender
{
public:
    virtual ~DataSender() = default;

    /**
     * @brief Process a single piece of data.
     *
     * @param data The abstract data to be sent.
     * @param callback The callback that will always be called to inform a success or failure.
     */
    virtual void processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback ) = 0;

    /**
     * @brief Process a single piece of data that has been persisted before.
     *
     * Normally this would be already serialized data that is ready to be sent.
     *
     * @param data The raw data to be sent.
     * @param metadata The metadata associated with the data.
     * @param callback The callback that will always be called to inform a success or failure.
     */
    virtual void processPersistedData( std::istream &data,
                                       const Json::Value &metadata,
                                       OnPersistedDataProcessedCallback callback ) = 0;
};

using DataSenderQueue = LockedQueue<std::shared_ptr<const DataToSend>>;

} // namespace IoTFleetWise
} // namespace Aws
