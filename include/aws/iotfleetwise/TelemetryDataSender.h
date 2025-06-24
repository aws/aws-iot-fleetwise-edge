// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <json/json.h>
#include <memory>
#include <string>
#include <vector>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "aws/iotfleetwise/snf/StreamManager.h"
#include <boost/optional/optional.hpp>
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

class TelemetryDataSerializer
{

public:
    TelemetryDataSerializer( ISender &mqttSender,
                             std::unique_ptr<DataSenderProtoWriter> protoWriter,
                             PayloadAdaptionConfig configUncompressed,
                             PayloadAdaptionConfig configCompressed
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                             ,
                             Aws::IoTFleetWise::Store::StreamManager *streamManager = nullptr
#endif
    );

    ~TelemetryDataSerializer() = default;

    void processData( const DataToSend &data, std::vector<TelemetryDataToPersist> &payloads );

private:
    ISender &mMQTTSender;
    std::unique_ptr<DataSenderProtoWriter> mProtoWriter;
    PayloadAdaptionConfig mConfigUncompressed;
    PayloadAdaptionConfig mConfigCompressed;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    Store::StreamManager *mStreamManager;
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
     */
    void transformTelemetryDataToProto( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                        ,
                                        const boost::optional<PartitionID> &partitionId
#endif
                                        ,
                                        std::vector<TelemetryDataToPersist> &payloads,
                                        std::function<bool( SignalID signalID )> signalFilter );

    /**
     * @brief If the serialized payload ends up larger than the maximum payload size, it will be split in half in a
     * recursive manner and re-serialized. This constant limits the number of times it will be split. I.e. 2 means it
     * first tries splitting in half, then if that's still too large it will try splitting into quarters before dropping
     * the payload.
     */
    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
    static constexpr unsigned UPLOAD_PROTO_RECURSION_LIMIT = 2;

    size_t serializeData( std::vector<TelemetryDataToPersist> &payloads
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                          ,
                          const boost::optional<PartitionID> &partitionId
#endif
                          ,
                          unsigned recursionLevel = 0 );

    template <typename T>
    void
    appendMessageToProto( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                          ,
                          const boost::optional<PartitionID> &partitionId
#endif
                          ,
                          T msg,
                          std::vector<TelemetryDataToPersist> &payloads )
    {
        mProtoWriter->append( msg );
        auto &config = triggeredCollectionSchemeData.metadata.compress ? mConfigCompressed : mConfigUncompressed;
        if ( mProtoWriter->getVehicleDataEstimatedSize() >= config.transmitSizeThreshold )
        {
            serializeData( payloads
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                           ,
                           partitionId
#endif
            );
            // Setup the next payload chunk
            mProtoWriter->setupVehicleData( triggeredCollectionSchemeData, mCollectionSchemeParams.eventID );
        }
    }

    /**
     * @brief Compresses data
     * @param input Input data string
     * @param output Where the result of compression will be saved to
     * @return True if compression succeeds
     */
    bool compress( std::string &input, std::string &output ) const;
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

    void processPersistedData( const uint8_t *buf,
                               size_t size,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

private:
    ISender &mMQTTSender;
    PayloadAdaptionConfig mConfigUncompressed;
    PayloadAdaptionConfig mConfigCompressed;
    TelemetryDataSerializer mSerializer;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    Aws::IoTFleetWise::Store::StreamManager *mStreamManager;
#endif

    /**
     * @brief Upload proto output.
     * @param callback Callback called after upload success / failure
     * @param payloads Payloads to be uploaded
     */
    void uploadProto( OnDataProcessedCallback callback, const std::vector<TelemetryDataToPersist> &payloads );
};

} // namespace IoTFleetWise
} // namespace Aws
