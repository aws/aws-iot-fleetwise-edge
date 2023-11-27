// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadManager.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <cstddef>
#include <memory>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

PayloadManager::PayloadManager( std::shared_ptr<CacheAndPersist> persistencyPtr )
    : mPersistencyPtr( std::move( persistencyPtr ) )
{
}

bool
PayloadManager::storeData( const std::uint8_t *buf,
                           size_t size,
                           const CollectionSchemeParams &collectionSchemeParams
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                           ,
                           const S3UploadParams &s3UploadParams
#endif
)
{
    if ( mPersistencyPtr == nullptr )
    {
        FWE_LOG_ERROR( "No CacheAndPersist module provided" );
        return false;
    }

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Payload buffer is empty" );
        TraceModule::get().incrementVariable( TraceVariable::PM_MEMORY_NULL );
        return false;
    }

    std::string filename;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    if ( s3UploadParams != S3UploadParams() )
    {
        filename = s3UploadParams.objectName;
    }
    else
#endif
    {
        filename = std::to_string( collectionSchemeParams.eventID ) + "-" +
                   std::to_string( collectionSchemeParams.triggerTime ) + ".bin";
    }

    ErrorCode writeStatus = mPersistencyPtr->write( buf, size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    if ( writeStatus != ErrorCode::SUCCESS )
    {
        FWE_LOG_ERROR( "Failed to persist collected data on disk" );
        TraceModule::get().incrementVariable( TraceVariable::PM_STORE_ERROR );
        if ( writeStatus == ErrorCode::MEMORY_FULL )
        {
            TraceModule::get().incrementVariable( TraceVariable::PM_MEMORY_INSUFFICIENT );
        }
        return false;
    }

    FWE_LOG_TRACE( "Payload of size : " + std::to_string( size ) + " Bytes has been successfully persisted in file " +
                   filename );

    storeMetadata( filename,
                   size,
                   collectionSchemeParams
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                   ,
                   s3UploadParams
#endif
    );
    return true;
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
bool
PayloadManager::storeIonData( std::unique_ptr<std::streambuf> streambuf, std::string filename )
{
    if ( streambuf == nullptr )
    {
        FWE_LOG_ERROR( "No stream provided" );
        return false;
    }

    ErrorCode writeStatus = mPersistencyPtr->write( std::move( streambuf ), DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    if ( writeStatus != ErrorCode::SUCCESS )
    {
        FWE_LOG_ERROR( "Failed to persist collected data on disk" );
        return false;
    }

    FWE_LOG_TRACE( "Payload has been successfully persisted in file " + filename );
    return true;
}
#endif

void
PayloadManager::storeMetadata( const std::string filename,
                               size_t size,
                               const CollectionSchemeParams &collectionSchemeParams
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                               ,
                               const S3UploadParams &s3UploadParams
#endif
)
{
    std::lock_guard<std::mutex> lock( mMetadataMutex );
    Json::Value metadata;
    metadata["filename"] = filename;
    metadata["payloadSize"] = static_cast<Json::Value::UInt64>( size );
    metadata["compressionRequired"] = collectionSchemeParams.compression;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    if ( s3UploadParams != S3UploadParams() )
    {
        metadata["s3UploadMetadata"]["bucketName"] = s3UploadParams.bucketName;
        metadata["s3UploadMetadata"]["bucketOwner"] = s3UploadParams.bucketOwner;
        metadata["s3UploadMetadata"]["region"] = s3UploadParams.region;
        metadata["s3UploadMetadata"]["uploadID"] = s3UploadParams.uploadID;
        metadata["s3UploadMetadata"]["partNumber"] = s3UploadParams.multipartID;
    }
#endif
    mPersistencyPtr->addMetadata( metadata );
    FWE_LOG_TRACE( "Metadata for file " + filename + " has been successfully added" );
}

ErrorCode
PayloadManager::retrievePayloadMetadata( Json::Value &files )
{
    if ( mPersistencyPtr == nullptr )
    {
        FWE_LOG_ERROR( "No CacheAndPersist module provided" );
        return ErrorCode::INVALID_DATA;
    }

    files = mPersistencyPtr->getMetadata();
    mPersistencyPtr->clearMetadata();
    FWE_LOG_TRACE( "Successfully retrieved metadata" );
    return ErrorCode::SUCCESS;
}

ErrorCode
PayloadManager::retrievePayload( uint8_t *buf, size_t size, const std::string &filename )
{
    if ( mPersistencyPtr == nullptr )
    {
        FWE_LOG_ERROR( "No CacheAndPersist module provided" );
        return ErrorCode::INVALID_DATA;
    }

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Buffer is empty" );
        return ErrorCode::INVALID_DATA;
    }

    ErrorCode status = mPersistencyPtr->read( buf, size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    // Delete file from disk
    mPersistencyPtr->erase( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    if ( status != ErrorCode::SUCCESS )
    {
        FWE_LOG_ERROR( "Failed to read persisted data from file " + filename );
        return status;
    }
    FWE_LOG_TRACE( "Successfully retrieved persisted data of size " + std::to_string( size ) + " Bytes from file " +
                   filename );
    return ErrorCode::SUCCESS;
}

} // namespace IoTFleetWise
} // namespace Aws
