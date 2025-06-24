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
#include <memory>
#include <snappy.h>
#include <string>
#include <utility>
#include <vector>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "aws/iotfleetwise/snf/StreamManager.h"
#include <boost/none.hpp>
#include <unordered_set>
#endif

namespace Aws
{
namespace IoTFleetWise
{

constexpr uint32_t MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG = 6;

TelemetryDataSerializer::TelemetryDataSerializer( ISender &mqttSender,
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

void
TelemetryDataSerializer::processData( const DataToSend &data, std::vector<TelemetryDataToPersist> &payloads )
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

#ifdef FWE_FEATURE_STORE_AND_FORWARD
        std::shared_ptr<const std::vector<Store::Partition>> partitions;
        if ( mStreamManager != nullptr )
        {
            partitions = mStreamManager->getPartitions( triggeredCollectionSchemeDataPtr->metadata.campaignArn );
        }

        if ( ( partitions != nullptr ) && ( !partitions->empty() ) )
        {
            // each partition requires its own chunk
            for ( const auto &partition : *partitions )
            {
                transformTelemetryDataToProto(
                    *triggeredCollectionSchemeDataPtr, partition.id, payloads, [&]( SignalID signalID ) -> bool {
                        return partition.signalIDs.find( signalID ) != partition.signalIDs.end();
                    } );
            }
        }
        else
#endif
        {
            transformTelemetryDataToProto( *triggeredCollectionSchemeDataPtr
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                           ,
                                           boost::none
#endif
                                           ,
                                           payloads,
                                           [&]( SignalID signalID ) -> bool {
                                               static_cast<void>( signalID );
                                               return true;
                                           } );
        }
    }
}

void
TelemetryDataSerializer::setCollectionSchemeParameters(
    const TriggeredCollectionSchemeData &triggeredCollectionSchemeData )
{
    mCollectionSchemeParams.persist = triggeredCollectionSchemeData.metadata.persist;
    mCollectionSchemeParams.compression = triggeredCollectionSchemeData.metadata.compress;
    mCollectionSchemeParams.priority = triggeredCollectionSchemeData.metadata.priority;
    mCollectionSchemeParams.eventID = triggeredCollectionSchemeData.eventID;
    mCollectionSchemeParams.triggerTime = triggeredCollectionSchemeData.triggerTime;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    mCollectionSchemeParams.campaignArn = triggeredCollectionSchemeData.metadata.campaignArn;
#endif
}

void
TelemetryDataSerializer::transformTelemetryDataToProto(
    const TriggeredCollectionSchemeData &triggeredCollectionSchemeData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    ,
    const boost::optional<PartitionID> &partitionId
#endif
    ,
    std::vector<TelemetryDataToPersist> &payloads,
    std::function<bool( SignalID signalID )> signalFilter )
{
    // Clear old data and setup metadata
    mPartNumber = 0;
    mProtoWriter->setupVehicleData( triggeredCollectionSchemeData, mCollectionSchemeParams.eventID );

    // Iterate through all the signals and add to the protobuf
    for ( const auto &signal : triggeredCollectionSchemeData.signals )
    {
        if ( signalFilter( signal.signalID )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
             // Filter out the raw data and internal signals
             && ( signal.value.type != SignalType::COMPLEX_SIGNAL ) &&
             ( ( signal.signalID & INTERNAL_SIGNAL_ID_BITMASK ) == 0 )
#endif
        )
        {
            appendMessageToProto( triggeredCollectionSchemeData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                  ,
                                  partitionId
#endif
                                  ,
                                  signal,
                                  payloads );
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
            appendMessageToProto( triggeredCollectionSchemeData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                  ,
                                  partitionId
#endif
                                  ,
                                  dtc,
                                  payloads );
        }
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    for ( const auto &object : triggeredCollectionSchemeData.uploadedS3Objects )
    {
        appendMessageToProto( triggeredCollectionSchemeData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                              ,
                              partitionId
#endif
                              ,
                              object,
                              payloads );
    }
#endif

    // Serialize any remaining messages
    if ( mProtoWriter->isVehicleDataAdded() )
    {
        serializeData( payloads
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                       ,
                       partitionId
#endif
        );
    }
}

bool
TelemetryDataSerializer::compress( std::string &input, std::string &output ) const
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

size_t
TelemetryDataSerializer::serializeData( std::vector<TelemetryDataToPersist> &payloads
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                        ,
                                        const boost::optional<PartitionID> &partitionId
#endif
                                        ,
                                        unsigned recursionLevel )
{
    auto protoOutput = std::make_shared<std::string>();
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    auto numberOfAppendedMessages = mProtoWriter->getNumberOfAppendedMessages();
#endif

    if ( !mProtoWriter->serializeVehicleData( protoOutput.get() ) )
    {
        FWE_LOG_ERROR( "Data cannot be uploaded due to serialization failure" );
        return 0;
    }

    if ( mCollectionSchemeParams.compression )
    {
        auto compressedProtoOutput = std::make_unique<std::string>();
        if ( !compress( *protoOutput, *compressedProtoOutput ) )
        {
            FWE_LOG_ERROR( "Data cannot be uploaded due to compression failure" );
            return 0;
        }
        protoOutput = std::move( compressedProtoOutput );
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
        serializeData( payloads
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                       ,
                       partitionId
#endif
                       ,
                       recursionLevel + 1 );
        mProtoWriter->mergeVehicleData( data );
        serializeData( payloads
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                       ,
                       partitionId
#endif
                       ,
                       recursionLevel + 1 );
        return protoOutput->size();
    }

    payloads.emplace_back( mCollectionSchemeParams,
                           mPartNumber,
                           protoOutput
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                           ,
                           partitionId,
                           numberOfAppendedMessages
#endif
    );

    auto payloadSizeLimitMin = ( maxSendSize * config.payloadSizeLimitMinPercent ) / 100;
    if ( ( recursionLevel == 0 ) && ( !protoOutput->empty() ) && ( protoOutput->size() < payloadSizeLimitMin ) )
    {
        config.transmitSizeThreshold =
            ( config.transmitSizeThreshold * ( 100 + config.transmitThresholdAdaptPercent ) ) / 100;
        FWE_LOG_TRACE( "Payload size " + std::to_string( protoOutput->size() ) + " below minimum limit " +
                       std::to_string( payloadSizeLimitMin ) + ". Increasing " +
                       ( mCollectionSchemeParams.compression ? "compressed" : "uncompressed" ) +
                       " transmit threshold by " + std::to_string( config.transmitThresholdAdaptPercent ) + "% to " +
                       std::to_string( config.transmitSizeThreshold ) );
    }

    mPartNumber++;
    FWE_LOG_INFO( "Payload has been created, size: " + std::to_string( protoOutput->size() ) +
                  " bytes, part number: " + std::to_string( mPartNumber ) +
                  ", compressed: " + std::to_string( mCollectionSchemeParams.compression ) +
                  ", vehicle data estimated size: " + std::to_string( mProtoWriter->getVehicleDataEstimatedSize() ) +
                  ", transmit size threshold: " + std::to_string( config.transmitSizeThreshold ) );

    return protoOutput->size();
}

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
    , mSerializer( mqttSender,
                   std::move( protoWriter ),
                   configUncompressed,
                   configCompressed
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                   ,
                   streamManager
#endif
                   )
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    , mStreamManager( streamManager )
#endif
{
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

    std::vector<TelemetryDataToPersist> payloads;
    mSerializer.processData( data, payloads );
    uploadProto( std::move( callback ), payloads );
}

void
TelemetryDataSender::uploadProto( OnDataProcessedCallback callback,
                                  const std::vector<TelemetryDataToPersist> &payloads )
{
    for ( const auto &payload : payloads )
    {
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        if ( payload.getPartitionId().has_value() && ( mStreamManager != nullptr ) )
        {
            mStreamManager->appendToStreams( payload );
        }
        else
#endif
        {
            std::shared_ptr<std::string> serializedData;
            try
            {
                serializedData = boost::get<std::shared_ptr<std::string>>( payload.getData() );
            }
            catch ( boost::bad_get & )
            {
                FWE_LOG_ERROR( "Payload is not a string" );
                return;
            }

            mMQTTSender.sendBuffer(
                mMQTTSender.getTopicConfig().telemetryDataTopic,
                reinterpret_cast<const uint8_t *>( serializedData->data() ),
                serializedData->size(),
                [callback,
                 collectionSchemeParams = payload.getCollectionSchemeParams(),
                 partNumber = payload.getPartNumber(),
                 serializedData]( ConnectivityError result ) {
                    if ( result == ConnectivityError::Success )
                    {
                        FWE_LOG_INFO( "Payload has been uploaded, size: " + std::to_string( serializedData->size() ) +
                                      " bytes, compressed: " + std::to_string( collectionSchemeParams.compression ) );

                        TraceModule::get().addToVariable( TraceVariable::DATA_FORWARD_BYTES, serializedData->size() );
                        TraceModule::get().incrementVariable( TraceVariable::VEHICLE_DATA_PUBLISH_COUNT );
                        callback( true, nullptr );
                    }
                    else
                    {
                        std::shared_ptr<const TelemetryDataToPersist> dataToPersist;
                        if ( collectionSchemeParams.persist )
                        {
                            dataToPersist = std::make_shared<TelemetryDataToPersist>( collectionSchemeParams,
                                                                                      partNumber,
                                                                                      serializedData
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                                                                      ,
                                                                                      boost::none,
                                                                                      0
#endif
                            );
                        }
                        callback( false, dataToPersist );
                    }
                } );

            TraceModule::get().sectionEnd( TraceSection::COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA );
            TraceModule::get().incrementVariable( TraceVariable::MQTT_SIGNAL_MESSAGES_SENT_OUT );
        }
    }
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

    mMQTTSender.sendBuffer( mMQTTSender.getTopicConfig().telemetryDataTopic,
                            buf,
                            size,
                            [callback = std::move( callback ), size]( ConnectivityError result ) {
                                if ( result != ConnectivityError::Success )
                                {
                                    callback( false );
                                    return;
                                }

                                FWE_LOG_INFO( "A Payload of size: " + std::to_string( size ) +
                                              " bytes has been uploaded" );
                                callback( true );
                            } );
}

} // namespace IoTFleetWise
} // namespace Aws
