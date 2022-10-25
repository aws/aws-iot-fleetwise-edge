// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadManager.h"
#include "TraceModule.h"
#include <cstring>
#include <memory>
#include <sstream>

using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;
using namespace Aws::IoTFleetWise::Platform::Linux;

PayloadManager::PayloadManager( std::shared_ptr<CacheAndPersist> persistencyPtr )
    : mPersistencyPtr( std::move( persistencyPtr ) )
{
}

bool
PayloadManager::preparePayload( uint8_t *const buf,
                                size_t size,
                                const std::string &data,
                                const struct CollectionSchemeParams &collectionSchemeParams )
{
    if ( buf == nullptr )
    {
        TraceModule::get().incrementVariable( TraceVariable::PM_MEMORY_NULL );
        mLogger.error( "PayloadManager::preparePayload", "Payload provided is empty" );
        return false;
    }

    // Prefix the data with header
    PayloadHeader payloadHdr = {};
    size_t hdrSize = sizeof( PayloadHeader );

    if ( size < ( data.size() + hdrSize ) )
    {
        TraceModule::get().incrementVariable( TraceVariable::PM_MEMORY_INSUFFICIENT );
        mLogger.error( "PayloadManager::preparePayload", "Payload Buffer size not sufficient" );
        return false;
    }

    // Add a payload header before writing to the file
    payloadHdr.size = data.size();
    payloadHdr.compressionRequired = collectionSchemeParams.compression;

    memcpy( &buf[0], &payloadHdr, hdrSize );
    memcpy( reinterpret_cast<char *>( &buf[hdrSize] ), data.data(), payloadHdr.size );

    return true;
}

bool
PayloadManager::storeData( const std::uint8_t *buf,
                           size_t size,
                           const struct CollectionSchemeParams &collectionSchemeParams )
{
    bool isDataPersisted = false;
    if ( collectionSchemeParams.persist )
    {
        mLogger.trace( "PayloadManager::storeData", "The schema activates data persistency" );

        std::string payload( reinterpret_cast<const char *>( buf ), size );
        std::string compressedData;
        // if compression was not specified in the collectionScheme, DCSender did not compress
        // compress it anyway for storage
        if ( !collectionSchemeParams.compression )
        {
            mLogger.trace( "PayloadManager::storeData",
                           "CollectionScheme does not activate compression, but will apply compression for local "
                           "persistency anyway" );
            if ( snappy::Compress( payload.data(), payload.size(), &compressedData ) == 0u )
            {
                TraceModule::get().incrementVariable( TraceVariable::PM_COMPRESS_ERROR );
                mLogger.error( "PayloadManager::storeData",
                               "Error occurred when compressing the payload. The payload is likely corrupted." );
                return isDataPersisted;
            }
        }
        else
        {
            // the payload was already compressed
            compressedData = payload;
        }

        size_t totalWriteSize = compressedData.size() + sizeof( PayloadHeader );
        // Allocate a bigger buffer to prefix payload header to the payload
        std::unique_ptr<uint8_t[]> writeBuffer( new uint8_t[totalWriteSize]() );
        // Add metadata to the payload before storage
        if ( !preparePayload( writeBuffer.get(), totalWriteSize, compressedData, collectionSchemeParams ) )
        {
            mLogger.error( "PayloadManager::storeData", "Error occurred during payload preparation" );
            return isDataPersisted;
        }

        ErrorCode status = mPersistencyPtr->write( writeBuffer.get(), totalWriteSize, DataType::EDGE_TO_CLOUD_PAYLOAD );

        if ( status == ErrorCode::SUCCESS )
        {
            // set the ErrorCode::SUCCESSful storage flag to true
            isDataPersisted = true;
            mLogger.trace( "PayloadManager::storeData",
                           "Payload of size : " + std::to_string( totalWriteSize ) +
                               " Bytes (header: " + std::to_string( sizeof( PayloadHeader ) ) +
                               ") has been ErrorCode::SUCCESSfully persisted" );
        }
        else
        {
            TraceModule::get().incrementVariable( TraceVariable::PM_STORE_ERROR );
            mLogger.error( "PayloadManager::storeData", "Failed to persist data on disk" );
        }
    }
    else
    {
        mLogger.trace( "PayloadManager::storeData", "CollectionScheme does not activate persistency on disk" );
    }
    return isDataPersisted;
}

ErrorCode
PayloadManager::retrieveData( std::vector<std::string> &data )
{
    size_t readSize = mPersistencyPtr->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD );

    if ( readSize == 0 )
    {
        return ErrorCode::EMPTY;
    }

    // Parsed data will be stored here
    std::unique_ptr<uint8_t[]> readBuffer( new uint8_t[readSize] );
    ErrorCode status = mPersistencyPtr->read( readBuffer.get(), readSize, DataType::EDGE_TO_CLOUD_PAYLOAD );

    if ( status != ErrorCode::SUCCESS )
    {
        return status;
    }
    else
    {
        size_t size = 0U;
        std::string dataString;

        // Read from the beginning of the buffer
        size_t pos = 0;

        while ( pos < readSize )
        {
            size_t j = 0;
            PayloadHeader payloadHdr{};
            memcpy( &payloadHdr, &( readBuffer.get()[pos] ), sizeof( PayloadHeader ) );
            pos += sizeof( PayloadHeader );

            // capture the size of the payload
            size = payloadHdr.size;

            // Clear the data string before parsing new payload
            dataString.clear();
            for ( j = 0; j < size && ( ( pos + j ) < readSize ); ++j )
            {
                dataString += ( readBuffer.get()[pos + j] ); // NOLINT(clang-diagnostic-sign-conversion)
            }

            std::string payloadData;
            // Since we always compress for storage,
            // uncompress if the collectionScheme did not require compression
            if ( !payloadHdr.compressionRequired )
            {
                mLogger.trace( "PayloadManager::retrieveData",
                               "CollectionScheme does not require compression, uncompress " +
                                   std::to_string( dataString.size() ) +
                                   " bytes before transmitting the "
                                   "persisted data." );
                if ( !snappy::Uncompress( dataString.data(), dataString.size(), &payloadData ) )
                {
                    mLogger.error(
                        "PayloadManager::retrieveData",
                        "Error occurred while un-compressing the payload from disk. The payload is likely corrupted." );
                    return ErrorCode::INVALID_DATA;
                }
            }
            else
            {
                payloadData = dataString;
            }

            data.emplace_back( payloadData );
            pos += j;
        }
    }
    mLogger.info( "PayloadManager::retrieveData",
                  "Payload of Size : " + std::to_string( readSize ) + " Bytes has been loaded from disk" );

    return ErrorCode::SUCCESS;
}
