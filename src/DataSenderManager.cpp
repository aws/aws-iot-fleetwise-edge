// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManager.h"
#include "CacheAndPersist.h"
#include "LoggingModule.h"
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <json/json.h>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

DataSenderManager::DataSenderManager( std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders,
                                      std::shared_ptr<ISender> mqttSender,
                                      std::shared_ptr<PayloadManager> payloadManager )
    : mDataSenders( std::move( dataSenders ) )
    , mMQTTSender( std::move( mqttSender ) )
    , mPayloadManager( std::move( payloadManager ) )
{
}

void
DataSenderManager::processData( std::shared_ptr<const DataToSend> data )
{
    if ( data == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is empty" );
        return;
    }
    auto sender = mDataSenders.find( data->getDataType() );
    if ( sender == mDataSenders.end() )
    {
        FWE_LOG_ERROR( "No sender configured for data type: " +
                       std::to_string( static_cast<int>( data->getDataType() ) ) );
        return;
    }

    sender->second->processData( data, [this]( bool success, std::shared_ptr<const DataToPersist> dataToPersist ) {
        if ( success )
        {
            FWE_LOG_TRACE( "Data successfully sent" );
        }
        else if ( dataToPersist != nullptr )
        {
            FWE_LOG_ERROR( "Failed to send data, persisting it" );
            if ( mPayloadManager != nullptr )
            {
                auto dataVariant = dataToPersist->getData();
                if ( dataVariant.type() == typeid( std::shared_ptr<std::string> ) )
                {
                    auto rawData = boost::get<std::shared_ptr<std::string>>( dataVariant );

                    Json::Value metadata;
                    metadata["type"] = senderDataTypeToString( dataToPersist->getDataType() );
                    Json::Value payloadSpecificMetadata = dataToPersist->getMetadata();
                    payloadSpecificMetadata["filename"] = dataToPersist->getFilename();
                    payloadSpecificMetadata["payloadSize"] = static_cast<Json::Value::UInt64>( rawData->size() );
                    metadata["payload"] = payloadSpecificMetadata;

                    mPayloadManager->storeData( reinterpret_cast<const uint8_t *>( rawData->data() ),
                                                rawData->size(),
                                                metadata,
                                                dataToPersist->getFilename() );
                }
                else if ( dataVariant.type() == typeid( std::shared_ptr<std::streambuf> ) )
                {
                    auto rawData = boost::get<std::shared_ptr<std::streambuf>>( dataVariant );
                    mPayloadManager->storeData( *rawData, dataToPersist->getMetadata(), dataToPersist->getFilename() );
                }
            }
        }
        else
        {
            FWE_LOG_ERROR(
                "Failed to send data, but persistency is not enabled for this type of data. Discarding it." );
        }
    } );
}

void
DataSenderManager::checkAndSendRetrievedData()
{
    // Retrieve the metadata from persistency library
    Json::Value files;
    if ( mPayloadManager == nullptr )
    {
        return;
    }
    ErrorCode status = mPayloadManager->retrievePayloadMetadata( files );

    if ( status != ErrorCode::SUCCESS )
    {
        FWE_LOG_ERROR( "Payload Metadata Retrieval Failed" );
        return;
    }
    FWE_LOG_TRACE( "Number of Payloads to transmit : " + std::to_string( files.size() ) );
    for ( const auto &item : files )
    {
        auto payloadType = SenderDataType::TELEMETRY;
        auto typeString = item["type"].asString();
        Json::Value payloadMetadata = item;
        if ( !typeString.empty() )
        {
            payloadMetadata = item["payload"];
            if ( !stringToSenderDataType( typeString, payloadType ) )
            {
                FWE_LOG_WARN( "Ignoring unsupported persisted data type: " + typeString )
                continue;
            }
        }
        else
        {
            FWE_LOG_TRACE( "Found legacy metadata. Assuming data type is Telemetry." )
        }

        auto sender = mDataSenders.find( payloadType );
        if ( sender == mDataSenders.end() )
        {
            FWE_LOG_ERROR( "No sender configured for persisted data type: " +
                           std::to_string( static_cast<int>( payloadType ) ) );
            continue;
        }

        // Retrieve the payload as stream so that it can be lazily read by the sender
        std::string filename = payloadMetadata["filename"].asString();
        size_t payloadSize = sizeof( size_t ) >= sizeof( uint64_t ) ? payloadMetadata["payloadSize"].asUInt64()
                                                                    : payloadMetadata["payloadSize"].asUInt();
        std::ifstream payload;
        if ( mPayloadManager->retrievePayloadLazily( payload, filename ) != ErrorCode::SUCCESS )
        {
            continue;
        }

        payload.seekg( 0, std::ios::end );
        auto fileSize = static_cast<size_t>( payload.tellg() );
        if ( payloadSize != fileSize )
        {
            FWE_LOG_ERROR( "Failed to read persisted data: requested size " + std::to_string( payloadSize ) +
                           " Bytes and actual size " + std::to_string( fileSize ) + " Bytes differ for file " +
                           filename );
            continue;
        }
        payload.seekg( 0, std::ios::beg );

        sender->second->processPersistedData( payload, payloadMetadata, [this, item, filename]( bool success ) {
            if ( !success )
            {
                FWE_LOG_ERROR( "Payload transmission for file " + filename + " failed. Saving its metadata back." );
                mPayloadManager->storeMetadata( item );
                return;
            }
            FWE_LOG_TRACE( "Payload from file " + filename + " has been successfully sent to the backend" );
            mPayloadManager->deletePayload( filename );
        } );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
