// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DataSenderManager.h"
#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <json/json.h>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

DataSenderManager::DataSenderManager(
    const std::unordered_map<SenderDataType, std::unique_ptr<DataSender>> &dataSenders, PayloadManager *payloadManager )
    : mDataSenders( dataSenders )
    , mPayloadManager( payloadManager )
{
}

void
DataSenderManager::processData( const DataToSend &data )
{
    auto sender = mDataSenders.find( data.getDataType() );
    if ( sender == mDataSenders.end() )
    {
        FWE_LOG_ERROR( "No sender configured for data type: " +
                       std::to_string( static_cast<int>( data.getDataType() ) ) );
        return;
    }

    sender->second->processData(
        data,
        // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
        [this]( bool success, std::shared_ptr<const DataToPersist> dataToPersist ) {
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
                        mPayloadManager->storeData(
                            *rawData, dataToPersist->getMetadata(), dataToPersist->getFilename() );
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
    unsigned int skippedPayloadsDueToSenderNotAlive = 0;
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

        if ( !sender->second->isAlive() )
        {
            skippedPayloadsDueToSenderNotAlive++;
            continue;
        }

        std::string filename = payloadMetadata["filename"].asString();
        size_t payloadSize = sizeof( size_t ) >= sizeof( uint64_t ) ? payloadMetadata["payloadSize"].asUInt64()
                                                                    : payloadMetadata["payloadSize"].asUInt();
        std::vector<uint8_t> payload;
        payload.resize( payloadSize );
        if ( mPayloadManager->retrievePayload( payload.data(), payloadSize, filename ) != ErrorCode::SUCCESS )
        {
            continue;
        }

        sender->second->processPersistedData(
            payload.data(),
            payloadSize,
            payloadMetadata,
            [this, item, filename = std::move( filename )]( bool success ) {
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

    if ( skippedPayloadsDueToSenderNotAlive > 0 )
    {
        FWE_LOG_TRACE( "Number of payloads skipped because the sender is not connected: " +
                       std::to_string( skippedPayloadsDueToSenderNotAlive ) );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
