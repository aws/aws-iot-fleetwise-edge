// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include "aws/iotfleetwise/TraceModule.h"
#include "vehicle_data.pb.h"
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <snappy.h>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

constexpr uint32_t MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG = 6;

class TelemetryDataToPersist : public DataToPersist
{
public:
    TelemetryDataToPersist( const CollectionSchemeParams &collectionSchemeParams,
                            unsigned partNumber,
                            std::shared_ptr<std::string> data )
        : mCollectionSchemeParams( collectionSchemeParams )
        , mPartNumber( partNumber )
        , mData( std::move( data ) )
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

private:
    CollectionSchemeParams mCollectionSchemeParams;
    unsigned mPartNumber;
    std::shared_ptr<std::string> mData;
};

TelemetryDataSender::TelemetryDataSender( ISender &mqttSender,
                                          std::unique_ptr<DataSenderProtoWriter> protoWriter,
                                          PayloadAdaptionConfig configUncompressed,
                                          PayloadAdaptionConfig configCompressed
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                          ,
                                          Aws::IoTFleetWise::Store::StreamManager *streamManager
#endif
                                          )
    : mMQTTSender( mqttSender )
    , mProtoWriter( std::move( protoWriter ) )
    , mConfigUncompressed( configUncompressed )
    , mConfigCompressed( configCompressed )
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    , mStreamManager( streamManager )
#endif
{
    mConfigUncompressed.transmitSizeThreshold =
        ( mMQTTSender.getMaxSendSize() * mConfigUncompressed.transmitThresholdStartPercent ) / 100;
    mConfigCompressed.transmitSizeThreshold =
        ( mMQTTSender.getMaxSendSize() * mConfigCompressed.transmitThresholdStartPercent ) / 100;
}

bool
TelemetryDataSender::isAlive()
{
    return mMQTTSender.isAlive();
}

void
TelemetryDataSender::processData( const DataToSend &data, OnDataProcessedCallback callback )
{
    // coverity[autosar_cpp14_a5_2_1_violation] Cast by design as we want the sender to know the concrete type.
    // coverity[autosar_cpp14_m5_2_3_violation] Cast by design as we want the sender to know the concrete type.
    // coverity[misra_cpp_2008_rule_5_2_3_violation] Cast by design as we want the sender to know the concrete type.
    auto triggeredCollectionSchemeDataPtr = dynamic_cast<const TriggeredCollectionSchemeData *>( &data );
    if ( triggeredCollectionSchemeDataPtr == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is not a valid TriggeredCollectionSchemeData" );
        return;
    }

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    auto result = Store::StreamManager::ReturnCode::STREAM_NOT_FOUND;
    if ( mStreamManager != nullptr )
    {
        result = mStreamManager->appendToStreams( *triggeredCollectionSchemeDataPtr );
    }
    if ( result == Store::StreamManager::ReturnCode::SUCCESS )
    {
        // Successfully appended
        return;
    }
    else if ( result == Store::StreamManager::ReturnCode::EMPTY_DATA )
    {
        FWE_LOG_INFO( "The trigger for Campaign:  " + triggeredCollectionSchemeDataPtr->metadata.campaignArn +
                      " activated eventID: " + std::to_string( triggeredCollectionSchemeDataPtr->eventID ) +
                      " but no data is available to ingest" );
        return;
    }
    else if ( result != Store::StreamManager::ReturnCode::STREAM_NOT_FOUND )
    {
        FWE_LOG_ERROR( "Failed to store FWE data with eventID " +
                       std::to_string( triggeredCollectionSchemeDataPtr->eventID ) );
        return;
    }
#endif

    std::string firstSignalValues = "[";
    uint32_t signalPrintCounter = 0;
    std::string firstSignalTimestamp;
    for ( auto &s : triggeredCollectionSchemeDataPtr->signals )
    {
        if ( firstSignalTimestamp.empty() )
        {
            firstSignalTimestamp = " first signal timestamp: " + std::to_string( s.receiveTime );
        }
        signalPrintCounter++;
        if ( signalPrintCounter > MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG )
        {
            firstSignalValues += " ...";
            break;
        }
        auto signalValue = s.getValue();
        firstSignalValues += std::to_string( s.signalID ) + ":";
        switch ( signalValue.getType() )
        {
        case SignalType::UINT8:
            firstSignalValues += std::to_string( signalValue.value.uint8Val ) + ",";
            break;
        case SignalType::INT8:
            firstSignalValues += std::to_string( signalValue.value.int8Val ) + ",";
            break;
        case SignalType::UINT16:
            firstSignalValues += std::to_string( signalValue.value.uint16Val ) + ",";
            break;
        case SignalType::INT16:
            firstSignalValues += std::to_string( signalValue.value.int16Val ) + ",";
            break;
        case SignalType::UINT32:
            firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
            break;
        case SignalType::INT32:
            firstSignalValues += std::to_string( signalValue.value.int32Val ) + ",";
            break;
        case SignalType::UINT64:
            firstSignalValues += std::to_string( signalValue.value.uint64Val ) + ",";
            break;
        case SignalType::INT64:
            firstSignalValues += std::to_string( signalValue.value.int64Val ) + ",";
            break;
        case SignalType::FLOAT:
            firstSignalValues += std::to_string( signalValue.value.floatVal ) + ",";
            break;
        case SignalType::DOUBLE:
            firstSignalValues += std::to_string( signalValue.value.doubleVal ) + ",";
            break;
        case SignalType::BOOLEAN:
            firstSignalValues += std::to_string( static_cast<int>( signalValue.value.boolVal ) ) + ",";
            break;
        case SignalType::STRING:
            firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
            break;
        case SignalType::UNKNOWN:
            // Signal of type UNKNOWN cannot be processed
            break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        case SignalType::COMPLEX_SIGNAL:
            firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
            break;
#endif
        }
    }
    firstSignalValues += "]";
    // Avoid invoking Data Collection Sender if there is nothing to send.
    if ( triggeredCollectionSchemeDataPtr->signals.empty() &&
         triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.empty()
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
         && triggeredCollectionSchemeDataPtr->uploadedS3Objects.empty()
#endif
    )
    {
        FWE_LOG_INFO( "The trigger for Campaign:  " + triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID +
                      " activated eventID: " + std::to_string( triggeredCollectionSchemeDataPtr->eventID ) +
                      " but no data is available to ingest" );
    }
    else
    {
        std::string message =
            "FWE data ready to send with eventID " + std::to_string( triggeredCollectionSchemeDataPtr->eventID ) +
            " from " + triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID +
            " Signals:" + std::to_string( triggeredCollectionSchemeDataPtr->signals.size() ) + " " + firstSignalValues +
            firstSignalTimestamp +
            " trigger timestamp: " + std::to_string( triggeredCollectionSchemeDataPtr->triggerTime ) +
            " DTCs:" + std::to_string( triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.size() )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            + " Uploaded S3 Objects: " + std::to_string( triggeredCollectionSchemeDataPtr->uploadedS3Objects.size() )
#endif
            ;
        FWE_LOG_INFO( message );

        setCollectionSchemeParameters( *triggeredCollectionSchemeDataPtr );
        transformTelemetryDataToProto( *triggeredCollectionSchemeDataPtr, callback );
    }
}

void
TelemetryDataSender::setCollectionSchemeParameters( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData )
{
    mCollectionSchemeParams.persist = triggeredCollectionSchemeData.metadata.persist;
    mCollectionSchemeParams.compression = triggeredCollectionSchemeData.metadata.compress;
    mCollectionSchemeParams.priority = triggeredCollectionSchemeData.metadata.priority;
    mCollectionSchemeParams.eventID = triggeredCollectionSchemeData.eventID;
    mCollectionSchemeParams.triggerTime = triggeredCollectionSchemeData.triggerTime;
}

void
TelemetryDataSender::transformTelemetryDataToProto( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData,
                                                    OnDataProcessedCallback callback )
{
    // Clear old data and setup metadata
    mPartNumber = 0;
    mProtoWriter->setupVehicleData( triggeredCollectionSchemeData, mCollectionSchemeParams.eventID );

    // Iterate through all the signals and add to the protobuf
    for ( const auto &signal : triggeredCollectionSchemeData.signals )
    {
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        // Filter out the raw data and internal signals
        if ( ( signal.value.type != SignalType::COMPLEX_SIGNAL ) &&
             ( ( signal.signalID & INTERNAL_SIGNAL_ID_BITMASK ) == 0 ) )
#endif
        {
            appendMessageToProto( triggeredCollectionSchemeData, signal, callback );
        }
    }

    // Add DTC info to the payload
    if ( triggeredCollectionSchemeData.mDTCInfo.hasItems() )
    {
        mProtoWriter->setupDTCInfo( triggeredCollectionSchemeData.mDTCInfo );
        const auto &dtcCodes = triggeredCollectionSchemeData.mDTCInfo.mDTCCodes;

        // Iterate through all the DTC codes and add to the protobuf
        for ( const auto &dtc : dtcCodes )
        {
            appendMessageToProto( triggeredCollectionSchemeData, dtc, callback );
        }
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    for ( const auto &object : triggeredCollectionSchemeData.uploadedS3Objects )
    {
        appendMessageToProto( triggeredCollectionSchemeData, object, callback );
    }
#endif

    // Serialize and transmit any remaining messages
    if ( mProtoWriter->isVehicleDataAdded() )
    {
        uploadProto( callback );
    }
}

bool
TelemetryDataSender::serialize( std::string &output )
{
    if ( !mProtoWriter->serializeVehicleData( &output ) )
    {
        FWE_LOG_ERROR( "Serialization failed" );
        return false;
    }
    return true;
}

bool
TelemetryDataSender::compress( std::string &input, std::string &output ) const
{
    if ( mCollectionSchemeParams.compression )
    {
        FWE_LOG_TRACE( "Compress the payload before transmitting since compression flag is true" );
        if ( snappy::Compress( input.data(), input.size(), &output ) == 0U )
        {
            FWE_LOG_TRACE( "Error in compressing the payload" );
            return false;
        }
    }
    return true;
}

#ifdef FWE_FEATURE_STORE_AND_FORWARD
void
TelemetryDataSender::processSerializedData( std::string &data, OnDataProcessedCallback callback )
{
    mMQTTSender.sendBuffer( mMQTTSender.getTopicConfig().telemetryDataTopic,
                            reinterpret_cast<const uint8_t *>( data.data() ),
                            data.size(),
                            [callback, data]( ConnectivityError result ) {
                                auto success = result == ConnectivityError::Success;
                                if ( success )
                                {
                                    FWE_LOG_INFO( "A Payload of size: " + std::to_string( data.size() ) +
                                                  " bytes has been uploaded" );
                                    TraceModule::get().addToVariable( TraceVariable::DATA_FORWARD_BYTES, data.size() );
                                    TraceModule::get().incrementVariable( TraceVariable::VEHICLE_DATA_PUBLISH_COUNT );
                                }
                                callback( success, nullptr );
                            } );
}
#endif

size_t
TelemetryDataSender::uploadProto( OnDataProcessedCallback callback, unsigned recursionLevel )
{
    auto protoOutput = std::make_shared<std::string>();

    if ( !serialize( *protoOutput ) )
    {
        FWE_LOG_ERROR( "Data cannot be uploaded due to serialization failure" );
        return 0;
    }

    if ( mCollectionSchemeParams.compression )
    {
        auto compressedProtoOutput = std::make_shared<std::string>();
        if ( !compress( *protoOutput, *compressedProtoOutput ) )
        {
            FWE_LOG_ERROR( "Data cannot be uploaded due to compression failure" );
            return 0;
        }
        protoOutput = compressedProtoOutput;
    }

    auto maxSendSize = mMQTTSender.getMaxSendSize();
    auto &config = mCollectionSchemeParams.compression ? mConfigCompressed : mConfigUncompressed;
    auto payloadSizeLimitMax = ( maxSendSize * config.payloadSizeLimitMaxPercent ) / 100;
    if ( protoOutput->size() > payloadSizeLimitMax )
    {
        config.transmitSizeThreshold =
            ( config.transmitSizeThreshold * ( 100 - config.transmitThresholdAdaptPercent ) ) / 100;
        FWE_LOG_TRACE( "Payload size " + std::to_string( protoOutput->size() ) + " above maximum limit " +
                       std::to_string( payloadSizeLimitMax ) + ". Decreasing " +
                       ( mCollectionSchemeParams.compression ? "compressed" : "uncompressed" ) +
                       " transmit threshold by " + std::to_string( config.transmitThresholdAdaptPercent ) + "% to " +
                       std::to_string( config.transmitSizeThreshold ) );
    }
    if ( protoOutput->size() > maxSendSize )
    {
        if ( recursionLevel >= UPLOAD_PROTO_RECURSION_LIMIT )
        {
            // If this happens frequently, look if a new repeated field type was added to the VehicleData protobuf
            // schema, but was not added to splitVehicleData and mergeVehicleData. This would cause the code below to
            // not actually make the payload any smaller.
            FWE_LOG_ERROR( "Payload dropped as it could not be split smaller than maximum payload size." );
            return 0;
        }
        FWE_LOG_TRACE(
            "Payload size " + std::to_string( protoOutput->size() ) + " exceeds the maximum payload size of " +
            std::to_string( maxSendSize ) + " for " +
            ( mCollectionSchemeParams.compression ? "compressed" : "uncompressed" ) +
            " data. Attempting to split in half and try again. Recursion level: " + std::to_string( recursionLevel ) );
        Schemas::VehicleDataMsg::VehicleData data;
        mProtoWriter->splitVehicleData( data );
        uploadProto( callback, recursionLevel + 1 );
        mProtoWriter->mergeVehicleData( data );
        uploadProto( callback, recursionLevel + 1 );
        return protoOutput->size();
    }

    mPartNumber++;
    mMQTTSender.sendBuffer(
        mMQTTSender.getTopicConfig().telemetryDataTopic,
        reinterpret_cast<const uint8_t *>( protoOutput->data() ),
        protoOutput->size(),
        [callback,
         collectionSchemeParams = mCollectionSchemeParams,
         partNumber = mPartNumber,
         protoOutput,
         vehicleDataEstimatedSize = mProtoWriter->getVehicleDataEstimatedSize(),
         threshold = config.transmitSizeThreshold]( ConnectivityError result ) {
            if ( result == ConnectivityError::Success )
            {
                FWE_LOG_INFO( "Payload has been uploaded, size: " + std::to_string( protoOutput->size() ) +
                              " bytes, part number: " + std::to_string( partNumber ) +
                              ", compressed: " + std::to_string( collectionSchemeParams.compression ) +
                              ", vehicle data estimated size: " + std::to_string( vehicleDataEstimatedSize ) +
                              ", transmit size threshold: " + std::to_string( threshold ) );
                TraceModule::get().addToVariable( TraceVariable::DATA_FORWARD_BYTES, protoOutput->size() );
                TraceModule::get().incrementVariable( TraceVariable::VEHICLE_DATA_PUBLISH_COUNT );
                callback( true, nullptr );
            }
            else
            {
                std::shared_ptr<const TelemetryDataToPersist> dataToPersist;
                if ( collectionSchemeParams.persist )
                {
                    dataToPersist =
                        std::make_shared<TelemetryDataToPersist>( collectionSchemeParams, partNumber, protoOutput );
                }
                callback( false, dataToPersist );
            }
        } );

    TraceModule::get().sectionEnd( TraceSection::COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA );
    TraceModule::get().incrementVariable( TraceVariable::MQTT_SIGNAL_MESSAGES_SENT_OUT );

    return protoOutput->size();
}

void
TelemetryDataSender::processPersistedData( const uint8_t *buf,
                                           size_t size,
                                           const Json::Value &metadata,
                                           OnPersistedDataProcessedCallback callback )
{
    static_cast<void>( metadata );

    if ( !mMQTTSender.isAlive() )
    {
        callback( false );
        return;
    }

    mMQTTSender.sendBuffer(
        mMQTTSender.getTopicConfig().telemetryDataTopic, buf, size, [callback, size]( ConnectivityError result ) {
            if ( result != ConnectivityError::Success )
            {
                callback( false );
                return;
            }

            FWE_LOG_INFO( "A Payload of size: " + std::to_string( size ) + " bytes has been uploaded" );
            callback( true );
        } );
}

} // namespace IoTFleetWise
} // namespace Aws
