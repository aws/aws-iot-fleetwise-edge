// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CacheAndPersist.h"
#include "LoggingModule.h"
#include <algorithm>
#include <boost/filesystem.hpp>
#include <cstdio>
#include <fstream>
#include <ios>
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

CacheAndPersist::CacheAndPersist( const std::string &partitionPath, size_t maxPartitionSize )
    : mPersistencyPath{ partitionPath }
    , mPersistencyWorkspace{ partitionPath +
                             ( ( ( partitionPath.empty() ) || ( partitionPath.back() == '/' ) ) ? "" : "/" ) +
                             PERSISTENCY_WORKSPACE }
    , mDecoderManifestFile{ mPersistencyWorkspace + DECODER_MANIFEST_FILE }
    , mCollectionSchemeListFile{ mPersistencyWorkspace + COLLECTION_SCHEME_LIST_FILE }
    , mPayloadMetadataFile{ mPersistencyWorkspace + PAYLOAD_METADATA_FILE }
    , mCollectedDataPath{ mPersistencyWorkspace + COLLECTED_DATA_FOLDER }
    , mMaxPersistencePartitionSize{ maxPartitionSize }
{
}

bool
CacheAndPersist::init()
{
    cleanupDeprecatedFiles();
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( ( !boost::filesystem::exists( mPayloadMetadataFile ) ) ||
         ( readMetadata( mPersistedMetadata ) != ErrorCode::SUCCESS ) )
    {
        mPersistedMetadata["version"] = METADATA_SCHEME_VERSION;
        mPersistedMetadata["files"] = Json::arrayValue;
    }
    else if ( mPersistedMetadata["version"] != METADATA_SCHEME_VERSION )
    {
        FWE_LOG_ERROR( "Metadata scheme version is not supported. Ignoring persisted files." )
        mPersistedMetadata["version"] = METADATA_SCHEME_VERSION;
        mPersistedMetadata["files"] = Json::arrayValue;
        static_cast<void>( erase( mPayloadMetadataFile ) );
    }
    else
    {
        FWE_LOG_TRACE( "Successfully read persisted metadata" );
    }

    // Create directory for persisted data if it doesn't exist
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( mPersistencyWorkspace ) )
    {
        try
        {
            // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and
            // and non-template function
            boost::filesystem::create_directory( mPersistencyWorkspace );
        }
        catch ( const boost::filesystem::filesystem_error &err )
        {
            FWE_LOG_ERROR( "Failed to create directory for persistency workspace: " + std::string( err.what() ) );
            return false;
        }
    }

    // Create directory for persisted data if it doesn't exist
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( mCollectedDataPath ) )
    {
        try
        {
            // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and
            // and non-template function
            boost::filesystem::create_directory( mCollectedDataPath );
        }
        catch ( const boost::filesystem::filesystem_error &err )
        {
            FWE_LOG_ERROR( "Failed to create directory for collected data: " + std::string( err.what() ) );
            return false;
        }
    }

    // Clean directory from files without metadata at startup
    cleanupPersistedData();

    FWE_LOG_INFO( "Persistency library successfully initialised" );
    return true;
}

ErrorCode
CacheAndPersist::write( const uint8_t *bufPtr, size_t size, DataType dataType, const std::string &filename )
{
    std::string path = getFileName( dataType );
    if ( dataType == DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        if ( filename.empty() )
        {
            FWE_LOG_ERROR( "Failed to persist data: filename for the payload is empty " );
            return ErrorCode::INVALID_DATATYPE;
        }
        else
        {
            path += filename;
        }
    }
    return write( bufPtr, size, path );
}

ErrorCode
CacheAndPersist::write( const uint8_t *bufPtr, size_t size, std::string &path )
{
    if ( bufPtr == nullptr )
    {
        FWE_LOG_ERROR( "Failed to persist data: buffer is empty" );
        return ErrorCode::INVALID_DATA;
    }

    if ( path.empty() )
    {
        FWE_LOG_ERROR( "Failed to persist data: path is empty" );
        return ErrorCode::INVALID_DATATYPE;
    }

    if ( getTotalSize() + size >= mMaxPersistencePartitionSize )
    {
        FWE_LOG_ERROR( "Failed to persist data: memory limit achieved" );
        return ErrorCode::MEMORY_FULL;
    }

    std::ofstream file( path.c_str(), std::ios_base::out | std::ios_base::binary );
    file.write( reinterpret_cast<const char *>( bufPtr ), static_cast<std::streamsize>( size ) );
    if ( !file.good() )
    {
        FWE_LOG_ERROR( "Failed to persist data: write to the file failed" );
        return ErrorCode::FILESYSTEM_ERROR;
    }
    return ErrorCode::SUCCESS;
}

void
CacheAndPersist::addMetadata( Json::Value &metadata )
{
    mPersistedMetadata["files"].append( metadata );
}

size_t
CacheAndPersist::getSize( DataType dataType, const std::string &filename )
{
    std::string path = getFileName( dataType );
    if ( dataType == DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        if ( filename.empty() )
        {
            FWE_LOG_ERROR( "Could not get filesize: filename for the payload is empty " );
            return INVALID_FILE_SIZE;
        }
        else
        {
            path += filename;
        }
    }
    return getSize( path );
}

size_t
CacheAndPersist::getSize( const std::string &path )
{
    if ( path.empty() )
    {
        FWE_LOG_ERROR( "Could not get filesize, path is empty" );
        return INVALID_FILE_SIZE;
    }
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( path ) )
    {
        FWE_LOG_TRACE( "File " + path + " does not exist" );
        return 0;
    }
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    std::uintmax_t fileSize = boost::filesystem::file_size( path );
    // In theory this should never happen as we only read the files created in this system, but it could
    // happen if someone copies persisted files from a different architecture (e.g. from arm64 to armhf).
    if ( fileSize > SIZE_MAX )
    {
        FWE_LOG_ERROR( "Filesize is larger than SIZE_MAX. File content can't be fully stored in memory." );
        return INVALID_FILE_SIZE;
    }
    return static_cast<size_t>( fileSize );
}

size_t
CacheAndPersist::getMetadataSize()
{
    // Estimate metadata size if one more file will be added
    return ( mPersistedMetadata["files"].size() + 1 ) * ESTIMATED_METADATA_SIZE_PER_FILE;
}

ErrorCode
CacheAndPersist::read( uint8_t *const readBufPtr, size_t size, DataType dataType, const std::string &filename )
{
    std::string path = getFileName( dataType );
    if ( dataType == DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        if ( filename.empty() )
        {
            FWE_LOG_ERROR( "Failed to read persisted data: filename for the payload is empty " );
            return ErrorCode::INVALID_DATATYPE;
        }
        else
        {
            path += filename;
        }
    }

    return read( readBufPtr, size, path );
}

ErrorCode
CacheAndPersist::read( uint8_t *const readBufPtr, size_t size, std::string &path ) const
{
    if ( readBufPtr == nullptr )
    {
        FWE_LOG_ERROR( "Failed to read persisted data: buffer is empty " );
        return ErrorCode::INVALID_DATA;
    }

    if ( size >= mMaxPersistencePartitionSize )
    {
        FWE_LOG_ERROR( "Failed to read persisted data: size is bigger than memory limit " );
        return ErrorCode::MEMORY_FULL;
    }

    if ( path.empty() )
    {
        FWE_LOG_ERROR( "Failed to read persisted data: path is empty " );
        return ErrorCode::INVALID_DATATYPE;
    }
    size_t filesize = getSize( path );
    if ( ( filesize == 0 ) || ( filesize == INVALID_FILE_SIZE ) )
    {
        FWE_LOG_ERROR( "Failed to read persisted data: file " + path + " is empty " );
        return ErrorCode::EMPTY;
    }

    if ( size != filesize )
    {
        FWE_LOG_ERROR( "Failed to read persisted data: requested size " + std::to_string( size ) +
                       " Bytes and actual size " + std::to_string( filesize ) + " Bytes differ" );
        return ErrorCode::INVALID_DATA;
    }

    std::ifstream file( path.c_str(), std::ios_base::binary | std::ios_base::in );
    file.clear();
    file.read( reinterpret_cast<char *>( readBufPtr ), static_cast<std::streamsize>( size ) );
    if ( file.fail() )
    {
        FWE_LOG_ERROR( "Error reading file" );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode
CacheAndPersist::readMetadata( Json::Value &metadata )
{
    try
    {
        std::ifstream jsonStream( mPayloadMetadataFile );
        jsonStream >> metadata;
    }
    catch ( ... )
    {
        FWE_LOG_ERROR( "Error reading JSON file with metadata" );
        static_cast<void>( erase( mPayloadMetadataFile ) );
        return ErrorCode::FILESYSTEM_ERROR;
    }
    return ErrorCode::SUCCESS;
}

ErrorCode
CacheAndPersist::erase( DataType dataType, const std::string &filename )
{
    std::string path = getFileName( dataType );
    if ( dataType == DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        if ( filename.empty() )
        {
            FWE_LOG_ERROR( "Failed to erase persisted data: filename for the edge to cloud payload is empty" );
            return ErrorCode::INVALID_DATATYPE;
        }
        else
        {
            path += filename;
        }
    }
    return erase( path );
}

ErrorCode
CacheAndPersist::erase( std::string &path )
{
    if ( path.empty() )
    {
        FWE_LOG_ERROR( "Failed to delete persisted file: path is empty " );
        return ErrorCode::INVALID_DATATYPE;
    }
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( path ) )
    {
        FWE_LOG_INFO( "File does not exist, nothing to delete: " + path );
        return ErrorCode::SUCCESS;
    }
    // Delete the file
    if ( std::remove( path.c_str() ) != 0 )
    {
        FWE_LOG_ERROR( "Failed to delete persisted file: remove failed" );
        return ErrorCode::FILESYSTEM_ERROR;
    }
    return ErrorCode::SUCCESS;
}

const char *
CacheAndPersist::getErrorString( ErrorCode err )
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

std::string
CacheAndPersist::getFileName( DataType dataType )
{
    switch ( dataType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        return mCollectionSchemeListFile;

    case DataType::DECODER_MANIFEST:
        return mDecoderManifestFile;

    case DataType::PAYLOAD_METADATA:
        return mPayloadMetadataFile;

    case DataType::EDGE_TO_CLOUD_PAYLOAD:
        return mCollectedDataPath;

    default:
        FWE_LOG_ERROR( "Invalid data type specified" );
        return "";
    }
}

std::uintmax_t
CacheAndPersist::getTotalSize()
{
    std::uintmax_t size = getMetadataSize();
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( mPersistencyWorkspace ) )
    {
        FWE_LOG_ERROR( "Directory " + mPersistencyWorkspace + " for persisted data does not exist" );
    }
    else
    {
        try
        {
            // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and
            // and non-template function
            for ( boost::filesystem::recursive_directory_iterator it( mPersistencyWorkspace );
                  it != boost::filesystem::recursive_directory_iterator();
                  ++it )
            {
                if ( !boost::filesystem::is_directory( *it ) )
                {
                    // actual metadata is stored in memory
                    if ( it->path().string() != mPayloadMetadataFile )
                    {
                        size += boost::filesystem::file_size( *it );
                    }
                }
            }
        }
        catch ( const boost::filesystem::filesystem_error &err )
        {
            FWE_LOG_ERROR( "Error getting file size: " + std::string( err.what() ) );
        }
    }
    return size;
}

void
CacheAndPersist::clearMetadata()
{
    mPersistedMetadata["files"] = Json::arrayValue;
}

ErrorCode
CacheAndPersist::cleanupPersistedData()
{
    FWE_LOG_TRACE( "Cleaning up persistency workspace" );
    std::vector<std::string> filenames;
    for ( const auto &file : mPersistedMetadata["files"] )
    {
        filenames.push_back( mCollectedDataPath + file["filename"].asString() );
    }
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( mPersistencyWorkspace ) )
    {
        FWE_LOG_ERROR( "Persistency directory " + mPersistencyWorkspace + " does not exist" );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    std::vector<std::string> filesToDelete;
    try
    {
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
        // non-template function
        for ( boost::filesystem::recursive_directory_iterator it( mPersistencyWorkspace );
              it != boost::filesystem::recursive_directory_iterator();
              ++it )
        {
            if ( !boost::filesystem::is_directory( *it ) )
            {
                std::string filename = it->path().string();
                if ( filename != mDecoderManifestFile && filename != mCollectionSchemeListFile &&
                     filename != mPayloadMetadataFile &&
                     ( std::find( filenames.begin(), filenames.end(), filename ) == filenames.end() ) )
                {
                    // Delete files after iterating over directory
                    filesToDelete.push_back( filename );
                }
            }
        }
    }
    catch ( const boost::filesystem::filesystem_error &err )
    {
        FWE_LOG_ERROR( "Error during clean up: " + std::string( err.what() ) );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    for ( auto &fileToDelete : filesToDelete )
    {
        static_cast<void>( erase( fileToDelete ) );
    }

    if ( !filesToDelete.empty() )
    {
        FWE_LOG_TRACE( "Persistency folder was cleaned up successfully" );
    }
    return ErrorCode::SUCCESS;
}

ErrorCode
CacheAndPersist::cleanupDeprecatedFiles()
{
    FWE_LOG_TRACE( "Cleaning up persistency folder from old and deprecated files" );
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
    // non-template function
    if ( !boost::filesystem::exists( mPersistencyPath ) )
    {
        FWE_LOG_ERROR( "Persistency directory " + mPersistencyPath + " does not exist" );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    std::vector<std::string> filesToDelete;
    try
    {
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
        // non-template function
        for ( boost::filesystem::directory_iterator it( mPersistencyPath );
              it != boost::filesystem::directory_iterator();
              ++it )
        {
            // Deleted all old files from the persistency directory. FWE is supposed to work only in
            // COLLECTED_DATA_FOLDER.
            if ( !boost::filesystem::is_directory( *it ) )
            {
                std::string filename = it->path().string();
                if ( ( filename == ( mPersistencyPath + DECODER_MANIFEST_FILE ) ) ||
                     ( filename == ( mPersistencyPath + COLLECTION_SCHEME_LIST_FILE ) ) ||
                     ( filename == ( mPersistencyPath + PAYLOAD_METADATA_FILE ) ) ||
                     ( filename == ( mPersistencyPath + DEPRECATED_COLLECTED_DATA_FILE ) ) )
                {
                    // Delete files after iterating over directory
                    filesToDelete.push_back( filename );
                }
            }
        }
    }
    catch ( const boost::filesystem::filesystem_error &err )
    {
        FWE_LOG_ERROR( "Error during clean up: " + std::string( err.what() ) );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    for ( auto &fileToDelete : filesToDelete )
    {
        static_cast<void>( erase( fileToDelete ) );
    }

    if ( !filesToDelete.empty() )
    {
        FWE_LOG_TRACE( "Deprecated files were successfully deleted from the persistency folder" );
    }
    return ErrorCode::SUCCESS;
}

bool
CacheAndPersist::writeMetadata( Json::Value &metadata )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    // coverity[autosar_cpp14_a20_8_5_violation] Calling newStreamWriter() is a recommended usage from boost
    // documentation
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    std::ofstream outputFileStream( mPayloadMetadataFile );
    writer->write( metadata, &outputFileStream );

    if ( !outputFileStream.good() )
    {
        FWE_LOG_ERROR( "Error writing metadata to the JSON file" );
        return false;
    }
    return true;
}

Json::Value
CacheAndPersist::getMetadata()
{
    return mPersistedMetadata["files"];
}

CacheAndPersist::~CacheAndPersist()
{
    writeMetadata( mPersistedMetadata );
    cleanupPersistedData();
}

} // namespace IoTFleetWise
} // namespace Aws
