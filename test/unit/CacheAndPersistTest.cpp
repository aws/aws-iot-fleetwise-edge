// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CacheAndPersist.h"
#include "Testing.h"
#include <boost/filesystem.hpp>
#include <cstdint>
#include <cstdlib>
#include <fstream> // IWYU pragma: keep
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

boost::filesystem::path
getFullPathForFile( DataType dataType, const std::string &filename )
{
    auto baseDir = getTempDir() / "FWE_Persistency";
    if ( dataType == DataType::EDGE_TO_CLOUD_PAYLOAD )
    {
        baseDir /= "CollectedData";
    }
    return baseDir / filename;
}

boost::filesystem::path
getSha1PathForFile( DataType dataType, const std::string &filename )
{
    auto sha1FilePath = getFullPathForFile( dataType, filename );
    sha1FilePath += ".sha1";
    return sha1FilePath;
}

std::string
getFileContent( const boost::filesystem::path &filename )
{
    std::ifstream ifs( filename.c_str() );
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

TEST( CacheAndPersistTest, generateChecksumFile )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();
    std::cout << "String size: " << size << std::endl;

    std::string filename = "testfile.bin";
    ASSERT_EQ(
        storage->write(
            reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
        ErrorCode::SUCCESS );

    auto sha1FilePath = getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( sha1FilePath ) );

    ASSERT_EQ( getFileContent( sha1FilePath ), "63ead68a5e69d980daeced67a8e2eb19dff75edb" );
}

TEST( CacheAndPersistTest, generateChecksumFileWhenWritingSmallStreambuf )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();

    std::string filename = "testfile.bin";
    std::stringbuf testStringbuf( testString );
    ASSERT_EQ( storage->write( testStringbuf, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), size );

    auto sha1FilePath = getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( sha1FilePath ) );

    ASSERT_EQ( getFileContent( sha1FilePath ), "63ead68a5e69d980daeced67a8e2eb19dff75edb" );
}

TEST( CacheAndPersistTest, generateChecksumFileWhenWritingLargeStreambuf )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString;
    {
        std::stringstream stream;
        for ( int i = 0; i < 100000; i++ )
        {
            stream << "1234567890" << std::endl;
        }
        testString = stream.str();
    }
    size_t size = testString.size();

    std::string filename = "testfile.bin";
    std::stringbuf testStringbuf( testString );
    ASSERT_EQ( storage->write( testStringbuf, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), size );

    auto sha1FilePath = getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( sha1FilePath ) );

    ASSERT_EQ( getFileContent( sha1FilePath ), "8589bba4c1cd34101cc5ab92ad3c32e752efa973" );
}

TEST( CacheAndPersistTest, rejectCorruptedCollectionScheme )
{
    auto storage = createCacheAndPersist();
    // create a test obj
    std::string testString = "Test CollectionScheme";
    size_t size = testString.size();

    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), size );

    auto collectionSchemePath = getFullPathForFile( DataType::COLLECTION_SCHEME_LIST, "CollectionSchemeList.bin" );
    ASSERT_TRUE( boost::filesystem::exists( collectionSchemePath ) );

    // Change just a single char and save the file
    testString = testString.substr( 0, size - 1 ) + "x";
    {
        std::ofstream ofs( collectionSchemePath.c_str() );
        ofs.write( testString.c_str(), testString.size() );
    }

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );

    // When it fails to read due to a checksum mismatch, the files should be deleted
    ASSERT_FALSE( boost::filesystem::exists( collectionSchemePath ) );
    ASSERT_FALSE( boost::filesystem::exists(
        getSha1PathForFile( DataType::COLLECTION_SCHEME_LIST, "CollectionSchemeList.bin" ) ) );
}

TEST( CacheAndPersistTest, rejectCorruptedEdgeToCloudPayload )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();

    std::string filename = "testfile.bin";
    ASSERT_EQ(
        storage->write(
            reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
        ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), size );

    auto payloadPath = getFullPathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( payloadPath ) );

    // Change just a single char and save the file
    testString = "x" + testString.substr( 1, size );
    {
        std::ofstream ofs( payloadPath.c_str() );
        ofs.write( testString.c_str(), testString.size() );
    }

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
               ErrorCode::INVALID_DATA );

    // When it fails to read due to a checksum mismatch, the files should be deleted
    ASSERT_FALSE( boost::filesystem::exists( payloadPath ) );
    ASSERT_FALSE( boost::filesystem::exists( getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ) ) );
}

TEST( CacheAndPersistTest, rejectCorruptedEdgeToCloudPayloadWrittenAsStreambuf )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();

    std::string filename = "testfile.bin";
    std::stringbuf testStringbuf( testString );
    ASSERT_EQ( storage->write( testStringbuf, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), size );

    auto payloadPath = getFullPathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( payloadPath ) );

    // Change just a single char and save the file
    testString = "x" + testString.substr( 1, size );
    {
        std::ofstream ofs( payloadPath.c_str() );
        ofs.write( testString.c_str(), testString.size() );
    }

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
               ErrorCode::INVALID_DATA );

    // When it fails to read due to a checksum mismatch, the files should be deleted
    ASSERT_FALSE( boost::filesystem::exists( payloadPath ) );
    ASSERT_FALSE( boost::filesystem::exists( getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ) ) );
}

TEST( CacheAndPersistTest, rejectFileWithInvalidChecksum )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();

    std::string filename = "testfile.bin";
    ASSERT_EQ(
        storage->write(
            reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
        ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), size );

    auto payloadPath = getFullPathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( payloadPath ) );

    // Modify the checksum file to make it invalid. This simulates the situation where the checksum
    // file itself wasn't written correctly.
    {
        std::ofstream ofs( getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ).c_str() );
        ofs.write( "invalid", 7 );
    }

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
               ErrorCode::INVALID_DATA );

    // When it fails to read due to a checksum mismatch, the files should be deleted
    ASSERT_FALSE( boost::filesystem::exists( payloadPath ) );
    ASSERT_FALSE( boost::filesystem::exists( getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ) ) );
}

TEST( CacheAndPersistTest, readFileWithoutChecksum )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();

    std::string filename = "testfile.bin";
    ASSERT_EQ(
        storage->write(
            reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
        ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), size );

    auto payloadPath = getFullPathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    ASSERT_TRUE( boost::filesystem::exists( payloadPath ) );

    // Now we remove the generate checksum file to simulate a persisted file that already existed
    // before we added checksum support. This should allow us to read the payload file successfully
    // to keep backward compatibility.
    ASSERT_TRUE( boost::filesystem::remove( getSha1PathForFile( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ) ) );

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), ErrorCode::SUCCESS );

    std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );

    ASSERT_STREQ( out.c_str(), testString.c_str() );
}

// Unit Tests for the collectionScheme persistency
TEST( CacheAndPersistTest, testCollectionSchemePersistency )
{
    auto storage = createCacheAndPersist();
    // create a test obj
    std::string testString = "Test CollectionScheme";
    size_t size = testString.size();

    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), size );

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

    std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );

    std::cout << "File contents: " << out << std::endl;
    ASSERT_STREQ( out.c_str(), testString.c_str() );
    ASSERT_EQ( storage->erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), 0 );
}

// Unit Tests for the DM persistency
TEST( CacheAndPersistTest, testDecoderManifestPersistency )
{
    auto storage = createCacheAndPersist();
    // create a test obj
    std::string testString = "Test Decoder Manifest";
    size_t size = testString.size();

    ASSERT_EQ(
        storage->write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::DECODER_MANIFEST ),
        ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::DECODER_MANIFEST ), size );

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::DECODER_MANIFEST ), ErrorCode::SUCCESS );
    std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );
    std::cout << "File contents: " << out << std::endl;
    ASSERT_STREQ( out.c_str(), testString.c_str() );
    ASSERT_EQ( storage->erase( DataType::DECODER_MANIFEST ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::DECODER_MANIFEST ), 0 );
}

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
TEST( CacheAndPersistTest, testStateTemplatesPersistency )
{
    auto storage = createCacheAndPersist();
    // create a test obj
    std::string testString = "Test StateTemplate";
    size_t size = testString.size();

    ASSERT_EQ(
        storage->write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::STATE_TEMPLATE_LIST ),
        ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::STATE_TEMPLATE_LIST ), size );

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::STATE_TEMPLATE_LIST ), ErrorCode::SUCCESS );
    std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );
    std::cout << "File contents: " << out << std::endl;
    ASSERT_STREQ( out.c_str(), testString.c_str() );
    ASSERT_EQ( storage->erase( DataType::STATE_TEMPLATE_LIST ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::STATE_TEMPLATE_LIST ), 0 );
}
#endif

TEST( CacheAndPersistTest, testWriteEmptyBuffer )
{
    auto storage = createCacheAndPersist();

    ASSERT_TRUE( storage->init() );
    ASSERT_EQ( storage->write( nullptr, 0, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );
}

// Check for the memory full condition
TEST( CacheAndPersistTest, testMemoryFull )
{
    auto storage = std::make_shared<CacheAndPersist>( getTempDir().string(), 2 );

    // Delete any existing contents
    ASSERT_EQ( storage->erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

    // create a test obj
    std::string testString = "test123";

    ASSERT_EQ( storage->write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                               testString.size(),
                               DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::MEMORY_FULL );
    ASSERT_EQ( storage->erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), 0 );
}

// Check for the invalid data type condition
TEST( CacheAndPersistTest, testInvalidDataType )
{
    auto storage = createCacheAndPersist();

    std::string testString = "test invalid data type";
    size_t size = testString.size();
    ASSERT_EQ(
        storage->write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::DEFAULT_DATA_TYPE ),
        ErrorCode::INVALID_DATATYPE );

    // Reading back the data
    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

    ASSERT_EQ( storage->getSize( DataType::DEFAULT_DATA_TYPE ), INVALID_FILE_SIZE );
    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::DEFAULT_DATA_TYPE ), ErrorCode::INVALID_DATATYPE );
    ASSERT_EQ( storage->erase( DataType::DEFAULT_DATA_TYPE ), ErrorCode::INVALID_DATATYPE );
}

// Tests writing multiple collectionSchemes, checks if it gets overwritten
TEST( CacheAndPersistTest, testCollectionSchemeListOverwrite )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString1 = "CollectionScheme 1234";
    std::string testString2 = "CollectionScheme 5";
    std::string testString3 = "CollectionScheme 678";

    size_t size1 = testString1.size();
    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString1.c_str() ), size1, DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), size1 );

    std::unique_ptr<uint8_t[]> readBufPtr1( new uint8_t[size1]() );
    ASSERT_EQ( storage->read( readBufPtr1.get(), size1, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
    std::string out1( reinterpret_cast<char *>( readBufPtr1.get() ), size1 );
    ASSERT_STREQ( out1.c_str(), testString1.c_str() );

    size_t size2 = testString2.size();

    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString2.c_str() ), size2, DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), size2 );

    std::unique_ptr<uint8_t[]> readBufPtr2( new uint8_t[size2]() );
    ASSERT_EQ( storage->read( readBufPtr2.get(), size2, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
    std::string out2( reinterpret_cast<char *>( readBufPtr2.get() ), size2 );
    ASSERT_STREQ( out2.c_str(), testString2.c_str() );

    size_t size3 = testString3.size();

    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString3.c_str() ), size3, DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), size3 );

    std::unique_ptr<uint8_t[]> readBufPtr3( new uint8_t[size3]() );
    ASSERT_EQ( storage->read( readBufPtr3.get(), size3, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
    std::string out3( reinterpret_cast<char *>( readBufPtr3.get() ), size3 );
    ASSERT_STREQ( out3.c_str(), testString3.c_str() );

    ASSERT_EQ( storage->erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
}

// Tests reading an empty file
TEST( CacheAndPersistTest, testReadEmptyFile )
{
    auto storage = createCacheAndPersist();

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[100]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), 100, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::EMPTY );
}

// Tests reading a big file
TEST( CacheAndPersistTest, testReadBigFile )
{
    auto storage = createCacheAndPersist();

    ASSERT_TRUE( storage->init() );
    size_t size = 200000;
    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

    ASSERT_EQ( storage->read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::MEMORY_FULL );
}

// Tests read with size mismatch
TEST( CacheAndPersistTest, testReadSizeMismatch )
{
    auto storage = createCacheAndPersist();

    std::string testString = "Wrong size test case";
    size_t size = testString.size();

    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::COLLECTION_SCHEME_LIST ),
               ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::COLLECTION_SCHEME_LIST ), size );

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

    ASSERT_EQ( storage->read( readBufPtr.get(), 123, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );
}

// Test Data Persistency
TEST( CacheAndPersistTest, testDataPersistency )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString = "Store this data - 1";
    size_t size = testString.size();
    std::cout << "String size: " << size << std::endl;

    std::string filename = "testfile.bin";
    ASSERT_EQ(
        storage->write(
            reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
        ErrorCode::SUCCESS );

    size_t readSize = storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
    std::cout << "testDataPersistency::readSize: " << readSize << std::endl;

    std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[readSize]() );
    ASSERT_EQ( storage->read( readBufPtr.get(), readSize, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
               ErrorCode::SUCCESS );

    std::string out( reinterpret_cast<const char *>( readBufPtr.get() ), readSize );
    ASSERT_STREQ( out.c_str(), testString.c_str() );
}

// Test multiple events being logged while the car is offline
TEST( CacheAndPersistTest, testMultipleEventDataPersistency )
{

    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString1 = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
    size_t size1 = testString1.size();
    std::cout << "String size: " << size1 << std::endl;

    std::string filename1 = "testfile1.bin";
    ASSERT_EQ( storage->write( reinterpret_cast<const uint8_t *>( testString1.c_str() ),
                               size1,
                               DataType::EDGE_TO_CLOUD_PAYLOAD,
                               filename1 ),
               ErrorCode::SUCCESS );

    std::string testString2 = "34567089!@#$%^&*()_+?><";
    size_t size2 = testString2.size();
    std::cout << "String size: " << size2 << std::endl;

    std::string filename2 = "testfile2.bin";
    ASSERT_EQ( storage->write( reinterpret_cast<const uint8_t *>( testString2.c_str() ),
                               size2,
                               DataType::EDGE_TO_CLOUD_PAYLOAD,
                               filename2 ),
               ErrorCode::SUCCESS );

    std::vector<uint8_t> payload1( size1 );
    ASSERT_EQ( storage->read( payload1.data(), payload1.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ),
               ErrorCode::SUCCESS );
    std::string out1( payload1.begin(), payload1.end() );
    ASSERT_STREQ( out1.c_str(), testString1.c_str() );

    ASSERT_EQ( storage->erase( DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ), 0 );

    std::vector<uint8_t> payload2( size2 );
    ASSERT_EQ( storage->read( payload2.data(), payload2.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename2 ),
               ErrorCode::SUCCESS );
    std::string out2( payload2.begin(), payload2.end() );
    ASSERT_STREQ( out2.c_str(), testString2.c_str() );

    ASSERT_EQ( storage->erase( DataType::EDGE_TO_CLOUD_PAYLOAD, filename2 ), ErrorCode::SUCCESS );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename2 ), 0 );
}

TEST( CacheAndPersistTest, testReadEmptyBuffer )
{
    auto storage = createCacheAndPersist();
    ASSERT_EQ( storage->read( nullptr, 0, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );
}

TEST( CacheAndPersistTest, testNoFilenameForPayload )
{
    auto storage = createCacheAndPersist();

    std::string testString = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
    size_t size = testString.size();

    std::vector<uint8_t> payload( size );

    ASSERT_EQ( storage->write(
                   reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD ),
               ErrorCode::INVALID_DATATYPE );
    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD ), INVALID_FILE_SIZE );
    ASSERT_EQ( storage->read( payload.data(), payload.size(), DataType::EDGE_TO_CLOUD_PAYLOAD ),
               ErrorCode::INVALID_DATATYPE );
    ASSERT_EQ( storage->erase( DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::INVALID_DATATYPE );
}

TEST( CacheAndPersistTest, testCleanUpFunction )
{
    auto storage = createCacheAndPersist();

    // create a test obj
    std::string testString1 = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
    size_t size1 = testString1.size();
    std::cout << "String size: " << size1 << std::endl;

    std::string filename1 = "testfile1.bin";
    ASSERT_EQ( storage->write( reinterpret_cast<const uint8_t *>( testString1.c_str() ),
                               size1,
                               DataType::EDGE_TO_CLOUD_PAYLOAD,
                               filename1 ),
               ErrorCode::SUCCESS );
    storage->clearMetadata();
}

TEST( CacheAndPersistTest, testInitAfterCleanUpFunction )
{
    auto storage = createCacheAndPersist();

    std::string filename1 = "testfile1.bin";

    ASSERT_EQ( storage->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ), 0 );
    ASSERT_EQ(
        storage->getSize( DataType::PAYLOAD_METADATA ),
        30 ); // 30 is a size for a default empty JSON metadata scheme, containing the version and the empty array
}

} // namespace IoTFleetWise
} // namespace Aws
