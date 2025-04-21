// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/Assert.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/version.hpp>
#include <cstdint>
#include <cstdio>
#include <fstream> // IWYU pragma: keep
#include <iomanip>
#include <ios>      // IWYU pragma: keep
#include <iostream> // IWYU pragma: keep
#include <memory>
#include <stdexcept>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

constexpr auto SHA1_DIGEST_SIZE_BYTES = sizeof( boost::uuids::detail::sha1::digest_type );

namespace
{
std::string
sha1DigestAsString( boost::uuids::detail::sha1::digest_type &digest )
{
    std::stringstream hashString;
#if BOOST_VERSION >= 108600
    for ( unsigned int i = 0; i < SHA1_DIGEST_SIZE_BYTES; i++ )
    {
        hashString << std::hex << std::setw( 2 ) << std::setfill( '0' ) << static_cast<unsigned int>( digest[i] );
    }
#else
    constexpr auto SHA1_DIGEST_SIZE_WORDS = SHA1_DIGEST_SIZE_BYTES / sizeof( uint32_t );
    for ( unsigned int i = 0; i < SHA1_DIGEST_SIZE_WORDS; i++ )
    {
        hashString << std::hex << std::setw( sizeof( uint32_t ) * 2 ) << std::setfill( '0' ) << digest[i];
    }
#endif

    return hashString.str();
}

std::string
calculateSha1( const uint8_t *buf, size_t size )
{
    boost::uuids::detail::sha1 sha1;
    try
    {
        sha1.process_bytes( buf, size );
    }
    catch ( const std::runtime_error &e )
    {
        // An exception is only thrown in case the input is too large. The max length for the input when calculating
        // SHA1 is 2^64 bits, so it should never happen in our case.
        FWE_FATAL_ASSERT( false, "Exception while calculating SHA1: " + std::string( e.what() ) );
    }

    boost::uuids::detail::sha1::digest_type digest{};
    sha1.get_digest( digest );
    return sha1DigestAsString( digest );
}
} // namespace

CacheAndPersist::CacheAndPersist( const std::string &partitionPath, size_t maxPartitionSize )
    : mPersistencyPath{ partitionPath }
    , mPersistencyWorkspace{ partitionPath +
                             ( ( ( partitionPath.empty() ) || ( partitionPath.back() == '/' ) ) ? "" : "/" ) +
                             PERSISTENCY_WORKSPACE }
    , mDecoderManifestFile( mPersistencyWorkspace + DECODER_MANIFEST_FILE )
    , mCollectionSchemeListFile( mPersistencyWorkspace + COLLECTION_SCHEME_LIST_FILE )
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    , mStateTemplateListFile( mPersistencyWorkspace + STATE_TEMPLATE_LIST_FILE )
    , mStateTemplateListMetadataFile( mPersistencyWorkspace + STATE_TEMPLATE_LIST_METADATA_FILE )
#endif
    , mPayloadMetadataFile( mPersistencyWorkspace + PAYLOAD_METADATA_FILE )
    , mCollectedDataPath( mPersistencyWorkspace + COLLECTED_DATA_FOLDER )
    , mMaxPersistencePartitionSize( maxPartitionSize )
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
    // Write the metadata to ensure the file is created in case it doesn't exist yet
    writeMetadata( mPersistedMetadata );

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
CacheAndPersist::write( const uint8_t *bufPtr, size_t size, const std::string &path )
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

    auto checksumStatus = writeChecksumForFile( path, calculateSha1( bufPtr, size ) );
    if ( checksumStatus != ErrorCode::SUCCESS )
    {
        return checksumStatus;
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

ErrorCode
CacheAndPersist::write( std::streambuf &streambuf, DataType dataType, const std::string &filename )
{
    if ( filename.empty() )
    {
        FWE_LOG_ERROR( "Failed to persist data: filename is empty" );
        return ErrorCode::INVALID_DATATYPE;
    }

    if ( dataType != DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        FWE_LOG_ERROR( "Failed to persist data: wrong datatype provided" );
        return ErrorCode::INVALID_DATATYPE;
    }

    boost::filesystem::path path{ mCollectedDataPath + filename };
    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and
    // and non-template function
    if ( !boost::filesystem::exists( path.parent_path().string() ) )
    {
        try
        {
            // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and
            // and non-template function
            boost::filesystem::create_directories( path.parent_path().string() );
        }
        catch ( const boost::filesystem::filesystem_error &err )
        {
            FWE_LOG_ERROR( "Failed to create directory for persistency of streamed vision system data: " +
                           std::string( err.what() ) );
            return ErrorCode::FILESYSTEM_ERROR;
        }
    }

    std::ofstream file( path.string(), std::ios::binary );

    boost::uuids::detail::sha1 sha1;

    while ( streambuf.sgetc() != std::char_traits<char>::eof() )
    {
        static const std::size_t CHUNK_SIZE = 4024;
        char chunk[CHUNK_SIZE];
        char *chunkPtr = chunk;
        auto count = streambuf.sgetn( chunkPtr, CHUNK_SIZE );
        try
        {
            sha1.process_bytes( chunkPtr, static_cast<size_t>( count ) );
        }
        catch ( const std::runtime_error &e )
        {
            // An exception is only thrown in case the input is too large. The max length for the input when calculating
            // SHA1 is 2^64 bits, so it should never happen in our case.
            FWE_FATAL_ASSERT( false, "Exception while calculating SHA1: " + std::string( e.what() ) );
        }
        file.write( chunkPtr, count );
    }

    boost::uuids::detail::sha1::digest_type digest{};
    sha1.get_digest( digest );
    auto checksumStatus = writeChecksumForFile( path.string(), sha1DigestAsString( digest ) );
    if ( checksumStatus != ErrorCode::SUCCESS )
    {
        return checksumStatus;
    }

    file.close();

    if ( !file.good() )
    {
        FWE_LOG_ERROR( "Failed to persist data: write to the file failed" );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    return ErrorCode::SUCCESS;
}

void
CacheAndPersist::addMetadata( const Json::Value &metadata )
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
CacheAndPersist::read( uint8_t *const readBufPtr, size_t size, const std::string &path ) const
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

    std::string expectedChecksum;
    auto checksumStatus = readChecksumForFile( path, expectedChecksum );
    if ( checksumStatus == ErrorCode::EMPTY )
    {
        // Skip the check as this is likely data that was persisted before checksum support was added.
        return ErrorCode::SUCCESS;
    }
    else if ( checksumStatus != ErrorCode::SUCCESS )
    {
        return checksumStatus;
    }

    std::string actualChecksum = calculateSha1( readBufPtr, size );
    if ( expectedChecksum != actualChecksum )
    {
        FWE_LOG_ERROR( "Checksum mismatch for file '" + path + "'. Expected SHA1: " + expectedChecksum +
                       ", actual SHA1: " + actualChecksum );
        erase( path );
        erase( getChecksumFilename( path ) );
        return ErrorCode::INVALID_DATA;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode
CacheAndPersist::readChecksumForFile( const std::string &path, std::string &result )
{
    auto checksumFilename = getChecksumFilename( path );
    // SHA1_DIGEST_SIZE is multiplied by 2 because we write it as a hex string
    if ( !boost::filesystem::exists( checksumFilename ) )
    {
        FWE_LOG_WARN( "Checksum file " + checksumFilename +
                      " doesn't exist. It won't be possible to detect if data is corrupted." );
        return ErrorCode::EMPTY;
    }

    constexpr auto checksumFileSize = static_cast<size_t>( SHA1_DIGEST_SIZE_BYTES * 2 );
    if ( getSize( checksumFilename ) != checksumFileSize )
    {
        // Don't do anything. The caller will eventually compare the values and detect the mismatch.
        FWE_LOG_WARN( "Invalid checksum file: " + checksumFilename );
        result = "";
        return ErrorCode::SUCCESS;
    }

    char checksumBuf[checksumFileSize];
    char *checksumBufPtr = checksumBuf;
    std::ifstream checksumFile( checksumFilename.c_str(), std::ios_base::in );
    checksumFile.clear();
    checksumFile.read( checksumBufPtr, static_cast<std::streamsize>( checksumFileSize ) );
    if ( checksumFile.fail() )
    {
        FWE_LOG_ERROR( "Error reading file: " + checksumFilename );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    result = std::string( checksumBufPtr, checksumBufPtr + checksumFileSize );
    return ErrorCode::SUCCESS;
}

ErrorCode
CacheAndPersist::writeChecksumForFile( const std::string &path, const std::string &checksum )
{
    auto checksumFilename = getChecksumFilename( path );
    std::ofstream checksumFile( checksumFilename.c_str(), std::ios_base::out );
    checksumFile << checksum;
    if ( !checksumFile.good() )
    {
        FWE_LOG_ERROR( "Failed to write file: " + checksumFilename );
        return ErrorCode::FILESYSTEM_ERROR;
    }

    return ErrorCode::SUCCESS;
}

std::string
CacheAndPersist::getChecksumFilename( const std::string &path )
{
    return path + ".sha1";
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
CacheAndPersist::erase( const std::string &path )
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
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    case DataType::STATE_TEMPLATE_LIST:
        return mStateTemplateListFile;
    case DataType::STATE_TEMPLATE_LIST_METADATA:
        return mStateTemplateListMetadataFile;
#endif

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
        // Handle the legacy metadata. file["filename"] is the old one and file["payload"]["filename"] the new one
        auto filename =
            file["filename"].asString().empty() ? file["payload"]["filename"].asString() : file["filename"].asString();
        filenames.push_back( mCollectedDataPath + filename );
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
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
                     filename != mStateTemplateListFile && filename != mStateTemplateListMetadataFile &&
#endif
                     ( std::find( filenames.begin(), filenames.end(), filename ) == filenames.end() ) )
                {
                    // Delete files after iterating over directory
                    // TODO: do not skip ion files but add the metadata for them so they don't get deleted
                    // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both
                    // template and and non-template function
                    if ( it->path().extension() != ".10n" ) // skip ion files
                    {
                        filesToDelete.push_back( filename );
                    }
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
        FWE_LOG_TRACE( "Deleting file " + fileToDelete );
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
