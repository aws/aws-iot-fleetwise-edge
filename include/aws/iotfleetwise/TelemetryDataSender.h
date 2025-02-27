// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <string>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "aws/iotfleetwise/snf/StreamManager.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

struct PayloadAdaptionConfig
{
    // Starting transmit threshold as percent of maximum payload size:
    unsigned transmitThresholdStartPercent{};
    // Adapt transmit threshold if payload size is outside this percentage range of the maximum payload size:
    unsigned payloadSizeLimitMinPercent{};
    unsigned payloadSizeLimitMaxPercent{};
    // Adapt transmit threshold by this amount when it is outside the range:
    unsigned transmitThresholdAdaptPercent{};
    // Transmit size threshold is set by TelemetryDataSender:
    size_t transmitSizeThreshold{};
};

class TelemetryDataSender : public DataSender
{

public:
    TelemetryDataSender( ISender &mqttSender,
                         std::unique_ptr<DataSenderProtoWriter> protoWriter,
                         PayloadAdaptionConfig configUncompressed,
                         PayloadAdaptionConfig configCompressed
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                         ,
                         Aws::IoTFleetWise::Store::StreamManager *streamManager = nullptr
#endif
    );

    ~TelemetryDataSender() override = default;

    bool isAlive() override;

    void processData( const DataToSend &data, OnDataProcessedCallback callback ) override;

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    void processSerializedData( std::string &data, OnDataProcessedCallback callback );
#endif

    void processPersistedData( const uint8_t *buf,
                               size_t size,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

private:
    ISender &mMQTTSender;
    std::unique_ptr<DataSenderProtoWriter> mProtoWriter;
    PayloadAdaptionConfig mConfigUncompressed;
    PayloadAdaptionConfig mConfigCompressed;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    Aws::IoTFleetWise::Store::StreamManager *mStreamManager;
#endif

    CollectionSchemeParams mCollectionSchemeParams;
    unsigned mPartNumber{ 0 }; // track how many payloads the data was split in

    /**
     * @brief Set up collectionSchemeParams struct
     * @param triggeredCollectionSchemeData collected data
     */
    void setCollectionSchemeParameters( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData );

    /**
     * @brief Put collected telemetry data into protobuf in chunks. Initiates serialization, compression, and
     * upload for each partition.
     * @param triggeredCollectionSchemeData collected data
     * @param callback callback function to be called after data is processed
     */
    void transformTelemetryDataToProto( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData,
                                        OnDataProcessedCallback callback );

    /**
     * @brief If the serialized payload ends up larger than the maximum payload size, it will be split in half in a
     * recursive manner and re-serialized. This constant limits the number of times it will be split. I.e. 2 means it
     * first tries splitting in half, then if that's still too large it will try splitting into quarters before dropping
     * the payload.
     */
    static constexpr unsigned UPLOAD_PROTO_RECURSION_LIMIT = 2;

    /**
     * @brief Serializes, compresses, and uploads proto output.
     * @param callback Callback called after upload success / failure
     * @param recursionLevel The level of recursion
     * @return payload size in bytes or zero on error
     */
    size_t uploadProto( OnDataProcessedCallback callback, unsigned recursionLevel = 0 );

    template <typename T>
    void
    appendMessageToProto( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData,
                          T msg,
                          OnDataProcessedCallback callback )
    {
        mProtoWriter->append( msg );
        auto &config = triggeredCollectionSchemeData.metadata.compress ? mConfigCompressed : mConfigUncompressed;
        if ( mProtoWriter->getVehicleDataEstimatedSize() >= config.transmitSizeThreshold )
        {
            auto payloadSize = uploadProto( callback );
            auto maxSendSize = mMQTTSender.getMaxSendSize();
            auto payloadSizeLimitMin = ( maxSendSize * config.payloadSizeLimitMinPercent ) / 100;
            if ( ( payloadSize > 0 ) && ( payloadSize < payloadSizeLimitMin ) )
            {
                config.transmitSizeThreshold =
                    ( config.transmitSizeThreshold * ( 100 + config.transmitThresholdAdaptPercent ) ) / 100;
                FWE_LOG_TRACE( "Payload size " + std::to_string( payloadSize ) + " below minimum limit " +
                               std::to_string( payloadSizeLimitMin ) + ". Increasing " +
                               ( triggeredCollectionSchemeData.metadata.compress ? "compressed" : "uncompressed" ) +
                               " transmit threshold by " + std::to_string( config.transmitThresholdAdaptPercent ) +
                               "% to " + std::to_string( config.transmitSizeThreshold ) );
            }
            // Setup the next payload chunk
            mProtoWriter->setupVehicleData( triggeredCollectionSchemeData, mCollectionSchemeParams.eventID );
        }
    }

    /**
     * @brief Serializes data
     * @param output Output string
     * @return True if serialization succeeds
     */
    bool serialize( std::string &output );

    /**
     * @brief Compresses data
     * @param input Input data string
     * @param output Where the result of compression will be saved to
     * @return True if compression succeeds
     */
    bool compress( std::string &input, std::string &output ) const;
};

} // namespace IoTFleetWise
} // namespace Aws
