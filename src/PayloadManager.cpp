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
                           const Json::Value &metadata,
                           const std::string &filename )
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

    mPersistencyPtr->addMetadata( metadata );
    FWE_LOG_TRACE( "Metadata for file " + filename + " has been successfully added" );

    return true;
}

bool
PayloadManager::storeData( std::streambuf &streambuf, const Json::Value &metadata, const std::string &filename )
{
    // TODO: We don't fully support persisting a stream payload yet, so only the stream content will be saved
    static_cast<void>( metadata );

    if ( mPersistencyPtr == nullptr )
    {
        FWE_LOG_ERROR( "No CacheAndPersist module provided" );
        return false;
    }

    ErrorCode writeStatus = mPersistencyPtr->write( streambuf, DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
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

    FWE_LOG_TRACE( "Payload has been successfully persisted in file " + filename );
    return true;
}

void
PayloadManager::storeMetadata( const Json::Value &metadata )
{
    mPersistencyPtr->addMetadata( metadata );
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

ErrorCode
PayloadManager::retrievePayloadLazily( std::ifstream &fileStream, const std::string &filename )
{
    return mPersistencyPtr->read( fileStream, DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
}

void
PayloadManager::deletePayload( const std::string &filename )
{
    if ( mPersistencyPtr == nullptr )
    {
        FWE_LOG_ERROR( "No CacheAndPersist module provided" );
        return;
    }

    auto status = mPersistencyPtr->erase( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    if ( status != ErrorCode::SUCCESS )
    {
        FWE_LOG_ERROR( "Failed to delete persisted file " + filename );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
