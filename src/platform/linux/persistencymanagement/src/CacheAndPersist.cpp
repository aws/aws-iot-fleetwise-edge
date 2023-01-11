// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CacheAndPersist.h"
#include <cstdio>
#include <fstream>
#include <ios>
#include <iostream>
#include <sys/stat.h>

using namespace Aws::IoTFleetWise::Platform::Linux::PersistencyManagement;

CacheAndPersist::CacheAndPersist( const std::string &partitionPath, size_t maxPartitionSize )
{
    // Define the file paths
    mDecoderManifestFile = partitionPath + DECODER_MANIFEST_FILE;
    mCollectionSchemeListFile = partitionPath + COLLECTION_SCHEME_LIST_FILE;
    mCollectedDataFile = partitionPath + COLLECTED_DATA_FILE;

    mMaxPersistencePartitionSize = maxPartitionSize;
}

bool
CacheAndPersist::init()
{
    if ( createFile( mDecoderManifestFile ) != ErrorCode::SUCCESS )
    {
        mLogger.error( "PersistencyManagement::init", "Failed to create decoder manifest file" );
        return false;
    }

    if ( createFile( mCollectionSchemeListFile ) != ErrorCode::SUCCESS )
    {
        mLogger.error( "PersistencyManagement::init", "Failed to create collectionScheme list file" );
        return false;
    }

    if ( createFile( mCollectedDataFile ) != ErrorCode::SUCCESS )
    {
        mLogger.error( "PersistencyManagement::init", "Failed to create collected data file" );

        return false;
    }

    mLogger.info( "PersistencyManagement::init", "Persistency library successfully initialised" );
    return true;
}

ErrorCode
CacheAndPersist::createFile( const std::string &fileName )
{
    ErrorCode status = ErrorCode::SUCCESS;

    // Check if the file exists
    std::ifstream existingFile( fileName.c_str(), std::ios_base::binary );

    if ( !existingFile.is_open() )
    {
        // File does not exist, create a new one
        std::ofstream newFile( fileName.c_str(), std::ios_base::binary | std::ios_base::app );
        if ( !newFile.is_open() )
        {
            status = ErrorCode::FILESYSTEM_ERROR;
        }
        else
        {
            newFile.close();
            status = ErrorCode::SUCCESS;
        }
    }
    else
    {
        existingFile.close();
        status = ErrorCode::SUCCESS;
    }

    return status;
}

ErrorCode
CacheAndPersist::write( const uint8_t *bufPtr, size_t size, DataType dataType )
{
    ErrorCode status = ErrorCode::SUCCESS;
    std::string fileName;
    std::ofstream file;

    if ( bufPtr == nullptr )
    {
        return ErrorCode::INVALID_DATA;
    }
    size_t collectionSchemeListSize = getSize( DataType::COLLECTION_SCHEME_LIST );
    size_t decoderManifestSize = getSize( DataType::DECODER_MANIFEST );
    size_t edgeToCloudPayloadSize = getSize( DataType::EDGE_TO_CLOUD_PAYLOAD );
    if ( collectionSchemeListSize + decoderManifestSize + edgeToCloudPayloadSize + size >=
         mMaxPersistencePartitionSize )
    {
        return ErrorCode::MEMORY_FULL;
    }

    switch ( dataType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        fileName = mCollectionSchemeListFile;
        break;

    case DataType::DECODER_MANIFEST:
        fileName = mDecoderManifestFile;
        break;

    case DataType::EDGE_TO_CLOUD_PAYLOAD:
        fileName = mCollectedDataFile;
        break;

    default:
        status = ErrorCode::INVALID_DATATYPE;
        mLogger.error( "PersistencyManagement::write", "Invalid data type specified" );
        return status;
    }

    if ( dataType == DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        // Payload is appended to the existing file
        file.open( fileName.c_str(), std::ios_base::binary | std::ios_base::app );
    }
    else
    {
        // CollectionScheme list and Decoder Manifest are overwritten
        file.open( fileName.c_str(), std::ios_base::binary );
    }

    if ( !file.is_open() )
    {
        status = ErrorCode::FILESYSTEM_ERROR;
        mLogger.error( "PersistencyManagement::write", "Could not open file" );
    }
    else
    {
        file.write( reinterpret_cast<const char *>( bufPtr ), static_cast<std::streamsize>( size ) );
        if ( !file.good() )
        {
            status = ErrorCode::FILESYSTEM_ERROR;
            mLogger.error( "PersistencyManagement::write", "Error writing to the file" );
        }
        file.close();
    }
    return status;
}

size_t
CacheAndPersist::getSize( DataType dataType )
{
    // get size of the file specified
    std::string fileName;
    size_t size = 0U;
    struct stat res = {};

    switch ( dataType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        fileName = mCollectionSchemeListFile;
        break;

    case DataType::DECODER_MANIFEST:
        fileName = mDecoderManifestFile;
        break;

    case DataType::EDGE_TO_CLOUD_PAYLOAD:
        fileName = mCollectedDataFile;
        break;

    default:
        mLogger.error( "PersistencyManagement::getSize", "Invalid data type specified" );
        return INVALID_FILE_SIZE;
    }

    // Get the file size
    if ( stat( fileName.c_str(), &res ) == 0 )
    {
        size = static_cast<size_t>( res.st_size );
    }

    return size;
}

ErrorCode
CacheAndPersist::read( uint8_t *const readBufPtr, size_t size, DataType dataType )
{
    ErrorCode status = ErrorCode::SUCCESS;
    std::string fileName;

    if ( readBufPtr == nullptr )
    {
        return ErrorCode::INVALID_DATA;
    }

    switch ( dataType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        fileName = mCollectionSchemeListFile;
        break;

    case DataType::DECODER_MANIFEST:
        fileName = mDecoderManifestFile;
        break;

    case DataType::EDGE_TO_CLOUD_PAYLOAD:
        fileName = mCollectedDataFile;
        break;

    default:
        status = ErrorCode::INVALID_DATATYPE;
        mLogger.error( "PersistencyManagement::read", "Invalid data type specified" );
        return status;
    }

    std::ifstream file( fileName.c_str(), std::ios_base::binary | std::ios_base::in );

    if ( !file.is_open() )
    {
        mLogger.error( "PersistencyManagement::read", "Error opening file" );
        status = ErrorCode::FILESYSTEM_ERROR;
    }
    else
    {
        size_t fileSize = std::min( mMaxPersistencePartitionSize, getSize( dataType ) );
        if ( fileSize == 0 )
        {
            status = ErrorCode::EMPTY;
        }
        else
        {
            file.read( reinterpret_cast<char *>( readBufPtr ),
                       static_cast<std::streamsize>( std::min( size, fileSize ) ) );
            // coverity[uninit_use_in_call : SUPPRESS]
            if ( file.fail() )
            {
                mLogger.error( "PersistencyManagement::read", "Error reading file" );
                status = ErrorCode::FILESYSTEM_ERROR;
            }
        }
        file.close();
    }

    return status;
}

ErrorCode
CacheAndPersist::erase( DataType dataType )
{
    ErrorCode status = ErrorCode::SUCCESS;
    std::string fileName;

    switch ( dataType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        fileName = mCollectionSchemeListFile;
        break;

    case DataType::DECODER_MANIFEST:
        fileName = mDecoderManifestFile;
        break;

    case DataType::EDGE_TO_CLOUD_PAYLOAD:
        fileName = mCollectedDataFile;
        break;

    default:
        status = ErrorCode::INVALID_DATATYPE;
        mLogger.error( "PersistencyManagement::erase", "Invalid data type specified" );
        return status;
    }

    // Delete the contents of the file
    std::ofstream file( fileName.c_str(), std::ios_base::binary | std::ios_base::out | std::ios_base::trunc );

    if ( !file.is_open() )
    {
        status = ErrorCode::FILESYSTEM_ERROR;
        mLogger.error( "PersistencyManagement::erase", "Error erasing the file" );
    }
    else
    {

        file.close();
    }

    return status;
}

const char *
ICacheAndPersist::getErrorString( ErrorCode err )
{
    switch ( err )
    {
    case ErrorCode::SUCCESS:
        return "ErrorCode::SUCCESS";
    case ErrorCode::MEMORY_FULL:
        return "MEMORY_FULL";
    case ErrorCode::EMPTY:
        return "EMPTY";
    case ErrorCode::FILESYSTEM_ERROR:
        return "FILESYSTEM_ERROR";
    case ErrorCode::INVALID_DATATYPE:
        return "INVALID_DATATYPE";
    case ErrorCode::INVALID_DATA:
        return "INVALID_DATA";
    default:
        return "UNKNOWN";
    }
}