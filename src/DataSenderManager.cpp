// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManager.h"
#include "CacheAndPersist.h"
#include "GeohashInfo.h"
#include "LoggingModule.h"
#include "OBDDataTypes.h"
#include "TraceModule.h"
#include <climits>
#include <json/json.h>
#include <snappy.h>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

DataSenderManager::DataSenderManager( std::shared_ptr<ISender> mqttSender,
                                      std::shared_ptr<PayloadManager> payloadManager,
                                      CANInterfaceIDTranslator &canIDTranslator,
                                      unsigned transmitThreshold )
    : mMQTTSender( std::move( mqttSender ) )
    , mPayloadManager( std::move( payloadManager ) )
    , mProtoWriter( canIDTranslator )
{
    mTransmitThreshold = ( transmitThreshold > 0U ) ? transmitThreshold : UINT_MAX;
}

void
DataSenderManager::processCollectedData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr )
{
    if ( triggeredCollectionSchemeDataPtr == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is empty" );
        return;
    }

    setCollectionSchemeParameters( triggeredCollectionSchemeDataPtr );

    transformTelemetryDataToProto( triggeredCollectionSchemeDataPtr );
}

void
DataSenderManager::setCollectionSchemeParameters(
    const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr )
{
    mCollectionSchemeParams.persist = triggeredCollectionSchemeDataPtr->metadata.persist;
    mCollectionSchemeParams.compression = triggeredCollectionSchemeDataPtr->metadata.compress;
    mCollectionSchemeParams.priority = triggeredCollectionSchemeDataPtr->metadata.priority;
    mCollectionSchemeParams.eventID = triggeredCollectionSchemeDataPtr->eventID;
    mCollectionSchemeParams.triggerTime = triggeredCollectionSchemeDataPtr->triggerTime;
    mCollectionSchemeID = triggeredCollectionSchemeDataPtr->metadata.collectionSchemeID;
}

void
DataSenderManager::transformTelemetryDataToProto(
    const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr )
{
    // Clear old data and setup metadata
    mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionSchemeParams.eventID );

    // Iterate through all the signals and add to the protobuf
    for ( const auto &signal : triggeredCollectionSchemeDataPtr->signals )
    {
        {
            appendMessageToProto( triggeredCollectionSchemeDataPtr, signal );
        }
    }

    // Iterate through all the raw CAN frames and add to the protobuf
    for ( const auto &canFrame : triggeredCollectionSchemeDataPtr->canFrames )
    {
        appendMessageToProto( triggeredCollectionSchemeDataPtr, canFrame );
    }

    // Add DTC info to the payload
    if ( triggeredCollectionSchemeDataPtr->mDTCInfo.hasItems() )
    {
        mProtoWriter.setupDTCInfo( triggeredCollectionSchemeDataPtr->mDTCInfo );
        const auto &dtcCodes = triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes;

        // Iterate through all the DTC codes and add to the protobuf
        for ( const auto &dtc : dtcCodes )
        {
            appendMessageToProto( triggeredCollectionSchemeDataPtr, dtc );
        }
    }

    // Add Geohash to the payload
    if ( triggeredCollectionSchemeDataPtr->mGeohashInfo.hasItems() )
    {
        appendMessageToProto( triggeredCollectionSchemeDataPtr, triggeredCollectionSchemeDataPtr->mGeohashInfo );
    }

    // Serialize and transmit any remaining messages
    if ( mProtoWriter.getVehicleDataMsgCount() >= 1U )
    {
        FWE_LOG_TRACE( "Queuing message for upload" );
        uploadProto();
    }
}

bool
DataSenderManager::serialize( std::string &output )
{
    // Note: a class member is used to store the serialized proto output to avoid heap fragmentation
    if ( !mProtoWriter.serializeVehicleData( &output ) )
    {
        FWE_LOG_ERROR( "Serialization failed" );
        return false;
    }
    return true;
}

bool
DataSenderManager::compress( std::string &input )
{
    if ( mCollectionSchemeParams.compression )
    {
        FWE_LOG_TRACE( "Compress the payload before transmitting since compression flag is true" );
        if ( snappy::Compress( input.data(), input.size(), &mCompressedProtoOutput ) == 0U )
        {
            FWE_LOG_TRACE( "Error in compressing the payload" );
            return false;
        }
    }
    return true;
}

ConnectivityError
DataSenderManager::send( const std::uint8_t *data, size_t size, std::shared_ptr<ISender> sender )
{
    if ( sender == nullptr )
    {
        FWE_LOG_ERROR( "No sender provided" );
        return ConnectivityError::NotConfigured;
    }

    ConnectivityError ret = sender->sendBuffer( data, size, mCollectionSchemeParams );
    if ( ret != ConnectivityError::Success )
    {
        FWE_LOG_ERROR( "Failed to send vehicle data with error: " + std::to_string( static_cast<int>( ret ) ) );
    }
    else
    {
        TraceModule::get().sectionEnd( TraceSection::COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA );
        TraceModule::get().incrementVariable( TraceVariable::MQTT_SIGNAL_MESSAGES_SENT_OUT );
        FWE_LOG_INFO( "A Payload of size: " + std::to_string( size ) + " bytes has been uploaded" );
    }
    return ret;
}

void
DataSenderManager::uploadProto()
{
    if ( !serialize( mProtoOutput ) )
    {
        FWE_LOG_ERROR( "Data cannot be uploaded due to serialization failure" );
        return;
    }
    if ( mCollectionSchemeParams.compression )
    {
        if ( !compress( mProtoOutput ) )
        {
            FWE_LOG_ERROR( "Data cannot be uploaded due to compression failure" );
            return;
        }
        static_cast<void>( send( reinterpret_cast<const uint8_t *>( mCompressedProtoOutput.data() ),
                                 mCompressedProtoOutput.size(),
                                 mMQTTSender ) );
    }
    else
    {
        static_cast<void>(
            send( reinterpret_cast<const uint8_t *>( mProtoOutput.data() ), mProtoOutput.size(), mMQTTSender ) );
    }
}

void
DataSenderManager::checkAndSendRetrievedData()
{
    // Retrieve the metadata from persistency library
    Json::Value files;
    ErrorCode status = mPayloadManager->retrievePayloadMetadata( files );

    if ( status == ErrorCode::SUCCESS )
    {
        FWE_LOG_TRACE( "Number of Payloads to transmit : " + std::to_string( files.size() ) );
        for ( const auto &file : files )
        {
            // Retrieve the payload data from persistency library
            std::string filename = file["filename"].asString();

            CollectionSchemeParams collectionSchemeParams;
            collectionSchemeParams.compression = file["compressionRequired"].asBool();
            collectionSchemeParams.persist = true;

            size_t payloadSize =
                sizeof( size_t ) >= sizeof( uint64_t ) ? file["payloadSize"].asUInt64() : file["payloadSize"].asUInt();
            if ( uploadPersistedFile( filename, payloadSize, collectionSchemeParams ) == ConnectivityError::Success )
            {
                FWE_LOG_TRACE( "Payload from file " + filename + " has been successfully sent to the backend" );
            }
            else
            {
                FWE_LOG_ERROR( "Payload transmission for file " + filename + " failed" );
            }
        }
        FWE_LOG_INFO( "Upload of persisted payloads is finished" );
    }
    else
    {
        FWE_LOG_ERROR( "Payload Metadata Retrieval Failed" );
    }
}

ConnectivityError
DataSenderManager::uploadPersistedFile( const std::string &filename,
                                        size_t size,
                                        CollectionSchemeParams collectionSchemeParams )
{
    auto res = mMQTTSender->sendFile( filename, size, collectionSchemeParams );
    if ( res != ConnectivityError::Success )
    {
        FWE_LOG_ERROR( "offboardconnectivity error " + std::to_string( static_cast<int>( res ) ) );
    }
    return res;
}

} // namespace IoTFleetWise
} // namespace Aws
