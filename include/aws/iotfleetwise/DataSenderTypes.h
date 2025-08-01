// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
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
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    COMMAND_RESPONSE = 2,
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    LAST_KNOWN_STATE = 3,
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
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    case SenderDataType::COMMAND_RESPONSE:
        return "CommandResponse";
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    case SenderDataType::LAST_KNOWN_STATE:
        return "LastKnownState";
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
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    else if ( dataType == "CommandResponse" )
    {
        output = SenderDataType::COMMAND_RESPONSE;
        return true;
    }
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    else if ( dataType == "LastKnownState" )
    {
        output = SenderDataType::LAST_KNOWN_STATE;
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
     * @brief Indicate if the sender is ready to send data
     *
     * This can be used to avoid sending data when the sender is not ready and thus avoid
     * unnecessary resource usage.
     * */
    virtual bool isAlive() = 0;

    /**
     * @brief Process a single piece of data.
     *
     * @param data The abstract data to be sent.
     * @param callback The callback that will always be called to inform a success or failure.
     */
    virtual void processData( const DataToSend &data, OnDataProcessedCallback callback ) = 0;

    /**
     * @brief Process a single piece of data that has been persisted before.
     *
     * Normally this would be already serialized data that is ready to be sent.
     *
     * @param buf The buffer containing the raw data to be sent.
     * @param size The size of the buffer containing the raw data to be sent.
     * @param metadata The metadata associated with the data.
     * @param callback The callback that will always be called to inform a success or failure.
     */
    virtual void processPersistedData( const uint8_t *buf,
                                       size_t size,
                                       const Json::Value &metadata,
                                       OnPersistedDataProcessedCallback callback ) = 0;
};

using DataSenderQueue = LockedQueue<std::shared_ptr<const DataToSend>>;

#ifdef FWE_FEATURE_STORE_AND_FORWARD
using PartitionID = uint32_t;
#endif

/**
 * @brief Struct that specifies the persistence and transmission attributes
 *        regarding the edge to cloud payload
 */
struct CollectionSchemeParams
{
    bool persist{ false };     // specifies if data needs to be persisted in case of connection loss
    bool compression{ false }; // specifies if data needs to be compressed for cloud
    uint32_t priority{ 0 };    // collectionScheme priority specified by the cloud
    uint64_t triggerTime{ 0 }; // timestamp of event ocurred
    uint32_t eventID{ 0 };     // event id
    SyncID collectionSchemeID;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    std::string campaignArn;
#endif
};

class TelemetryDataToPersist : public DataToPersist
{
public:
    TelemetryDataToPersist( CollectionSchemeParams collectionSchemeParams,
                            unsigned partNumber,
                            std::shared_ptr<std::string> data
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                            ,
                            boost::optional<PartitionID> partitionId,
                            size_t numberOfSignals
#endif
                            )
        : mCollectionSchemeParams( std::move( collectionSchemeParams ) )
        , mPartNumber( partNumber )
        , mData( std::move( data ) )
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        , mPartitionId( partitionId )
        , mNumberOfSignals( numberOfSignals )
#endif
    {
    }

    SenderDataType
    getDataType() const override
    {
        return SenderDataType::TELEMETRY;
    }

    Json::Value
    getMetadata() const override
    {
        Json::Value metadata;
        metadata["compressionRequired"] = mCollectionSchemeParams.compression;
        return metadata;
    }

    std::string
    getFilename() const override
    {
        return std::to_string( mCollectionSchemeParams.eventID ) + "-" +
               std::to_string( mCollectionSchemeParams.triggerTime ) + "-" + std::to_string( mPartNumber ) + ".bin";
    };

    boost::variant<std::shared_ptr<std::string>, std::shared_ptr<std::streambuf>>
    getData() const override
    {
        return mData;
    }

    const CollectionSchemeParams &
    getCollectionSchemeParams() const
    {
        return mCollectionSchemeParams;
    }

    unsigned
    getPartNumber() const
    {
        return mPartNumber;
    }

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    boost::optional<PartitionID>
    getPartitionId() const
    {
        return mPartitionId;
    };

    size_t
    getNumberOfSignals() const
    {
        return mNumberOfSignals;
    }
#endif

private:
    CollectionSchemeParams mCollectionSchemeParams;
    unsigned mPartNumber;
    std::shared_ptr<std::string> mData;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    boost::optional<PartitionID> mPartitionId;
    size_t mNumberOfSignals{ 0 };
#endif
};

} // namespace IoTFleetWise
} // namespace Aws
