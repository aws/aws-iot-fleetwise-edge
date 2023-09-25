// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CacheAndPersist.h"
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

// Unit Tests for the collectionScheme persistency
TEST( CacheAndPersistTest, testCollectionSchemePersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );
        ASSERT_TRUE( storage.init() );
        // Delete any existing contents
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

        // create a test obj
        std::string testString = "Test CollectionScheme";
        size_t size = testString.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                                  size,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

        std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );

        std::cout << "File contents: " << out << std::endl;
        ASSERT_STREQ( out.c_str(), testString.c_str() );
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), 0 );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Unit Tests for the DM persistency
TEST( CacheAndPersistTest, testDecoderManifestPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );
        ASSERT_TRUE( storage.init() );
        // create a test obj
        std::string testString = "Test Decoder Manifest";
        size_t size = testString.size();

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::DECODER_MANIFEST ),
            ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::DECODER_MANIFEST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, DataType::DECODER_MANIFEST ), ErrorCode::SUCCESS );
        std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );
        std::cout << "File contents: " << out << std::endl;
        ASSERT_STREQ( out.c_str(), testString.c_str() );
        ASSERT_EQ( storage.erase( DataType::DECODER_MANIFEST ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::DECODER_MANIFEST ), 0 );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

TEST( CacheAndPersistTest, testWriteEmptyBuffer )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );
        ASSERT_EQ( storage.write( nullptr, 0, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Check for the memory full condition
TEST( CacheAndPersistTest, testMemoryFull )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 2 );

        ASSERT_TRUE( storage.init() );
        // Delete any existing contents
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

        // create a test obj
        std::string testString = "test123";

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                                  testString.size(),
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::MEMORY_FULL );
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), 0 );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Check for the invalid data type condition
TEST( CacheAndPersistTest, testInvalidDataType )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );

        std::string testString = "test invalid data type";
        size_t size = testString.size();
        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::DEFAULT_DATA_TYPE ),
            ErrorCode::INVALID_DATATYPE );

        // Reading back the data
        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

        ASSERT_EQ( storage.getSize( DataType::DEFAULT_DATA_TYPE ), INVALID_FILE_SIZE );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, DataType::DEFAULT_DATA_TYPE ), ErrorCode::INVALID_DATATYPE );
        ASSERT_EQ( storage.erase( DataType::DEFAULT_DATA_TYPE ), ErrorCode::INVALID_DATATYPE );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Tests writing multiple collectionSchemes, checks if it gets overwritten
TEST( CacheAndPersistTest, testCollectionSchemeListOverwrite )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );

        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

        // create a test obj
        std::string testString1 = "CollectionScheme 1234";
        std::string testString2 = "CollectionScheme 5";
        std::string testString3 = "CollectionScheme 678";

        size_t size1 = testString1.size();
        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString1.c_str() ),
                                  size1,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size1 );

        std::unique_ptr<uint8_t[]> readBufPtr1( new uint8_t[size1]() );
        ASSERT_EQ( storage.read( readBufPtr1.get(), size1, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        std::string out1( reinterpret_cast<char *>( readBufPtr1.get() ), size1 );
        ASSERT_STREQ( out1.c_str(), testString1.c_str() );

        size_t size2 = testString2.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString2.c_str() ),
                                  size2,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size2 );

        std::unique_ptr<uint8_t[]> readBufPtr2( new uint8_t[size2]() );
        ASSERT_EQ( storage.read( readBufPtr2.get(), size2, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        std::string out2( reinterpret_cast<char *>( readBufPtr2.get() ), size2 );
        ASSERT_STREQ( out2.c_str(), testString2.c_str() );

        size_t size3 = testString3.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString3.c_str() ),
                                  size3,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size3 );

        std::unique_ptr<uint8_t[]> readBufPtr3( new uint8_t[size3]() );
        ASSERT_EQ( storage.read( readBufPtr3.get(), size3, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        std::string out3( reinterpret_cast<char *>( readBufPtr3.get() ), size3 );
        ASSERT_STREQ( out3.c_str(), testString3.c_str() );

        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Tests reading an empty file
TEST( CacheAndPersistTest, testReadEmptyFile )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[100]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), 100, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::EMPTY );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Tests reading a big file
TEST( CacheAndPersistTest, testReadBigFile )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );
        size_t size = 200000;
        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

        ASSERT_EQ( storage.read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::MEMORY_FULL );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Tests read with size mismatch
TEST( CacheAndPersistTest, testReadSizeMismatch )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );

        std::string testString = "Wrong size test case";
        size_t size = testString.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                                  size,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

        ASSERT_EQ( storage.read( readBufPtr.get(), 123, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Test Data Persistency
TEST( CacheAndPersistTest, testDataPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        // Delete any existing contents
        ASSERT_TRUE( storage.init() );

        // create a test obj
        std::string testString = "Store this data - 1";
        size_t size = testString.size();
        std::cout << "String size: " << size << std::endl;

        std::string filename = "testfile.bin";
        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                                  size,
                                  DataType::EDGE_TO_CLOUD_PAYLOAD,
                                  filename ),
                   ErrorCode::SUCCESS );

        size_t readSize = storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
        std::cout << "testDataPersistency::readSize: " << readSize << std::endl;

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[readSize]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), readSize, DataType::EDGE_TO_CLOUD_PAYLOAD, filename ),
                   ErrorCode::SUCCESS );

        std::string out( reinterpret_cast<const char *>( readBufPtr.get() ), readSize );
        ASSERT_STREQ( out.c_str(), testString.c_str() );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

// Test multiple events being logged while the car is offline
TEST( CacheAndPersistTest, testMultipleEventDataPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_EQ( storage.erase( DataType::PAYLOAD_METADATA ), ErrorCode::SUCCESS );
        ASSERT_TRUE( storage.init() );

        // create a test obj
        std::string testString1 = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
        size_t size1 = testString1.size();
        std::cout << "String size: " << size1 << std::endl;

        std::string filename1 = "testfile1.bin";
        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString1.c_str() ),
                                  size1,
                                  DataType::EDGE_TO_CLOUD_PAYLOAD,
                                  filename1 ),
                   ErrorCode::SUCCESS );

        std::string testString2 = "34567089!@#$%^&*()_+?><";
        size_t size2 = testString2.size();
        std::cout << "String size: " << size2 << std::endl;

        std::string filename2 = "testfile2.bin";
        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString2.c_str() ),
                                  size2,
                                  DataType::EDGE_TO_CLOUD_PAYLOAD,
                                  filename2 ),
                   ErrorCode::SUCCESS );

        std::vector<uint8_t> payload1( size1 );
        ASSERT_EQ( storage.read( payload1.data(), payload1.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ),
                   ErrorCode::SUCCESS );
        std::string out1( payload1.begin(), payload1.end() );
        ASSERT_STREQ( out1.c_str(), testString1.c_str() );

        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ), 0 );

        std::vector<uint8_t> payload2( size2 );
        ASSERT_EQ( storage.read( payload2.data(), payload2.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename2 ),
                   ErrorCode::SUCCESS );
        std::string out2( payload2.begin(), payload2.end() );
        ASSERT_STREQ( out2.c_str(), testString2.c_str() );

        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD, filename2 ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename2 ), 0 );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

TEST( CacheAndPersistTest, testReadEmptyBuffer )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );
        ASSERT_EQ( storage.read( nullptr, 0, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::INVALID_DATA );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

TEST( CacheAndPersistTest, testNoFilenameForPayload )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );

        std::string testString = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
        size_t size = testString.size();

        std::vector<uint8_t> payload( size );

        ASSERT_EQ( storage.write(
                       reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD ),
                   ErrorCode::INVALID_DATATYPE );
        ASSERT_EQ( storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD ), INVALID_FILE_SIZE );
        ASSERT_EQ( storage.read( payload.data(), payload.size(), DataType::EDGE_TO_CLOUD_PAYLOAD ),
                   ErrorCode::INVALID_DATATYPE );
        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::INVALID_DATATYPE );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

TEST( CacheAndPersistTest, testCleanUpFunction )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );

        ASSERT_TRUE( storage.init() );

        // create a test obj
        std::string testString1 = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
        size_t size1 = testString1.size();
        std::cout << "String size: " << size1 << std::endl;

        std::string filename1 = "testfile1.bin";
        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString1.c_str() ),
                                  size1,
                                  DataType::EDGE_TO_CLOUD_PAYLOAD,
                                  filename1 ),
                   ErrorCode::SUCCESS );
        storage.clearMetadata();
    }
}

TEST( CacheAndPersistTest, testInitAfterCleanUpFunction )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        CacheAndPersist storage( std::string( buffer ) + "/Persistency", 131072 );
        ASSERT_TRUE( storage.init() );

        std::string filename1 = "testfile1.bin";

        ASSERT_EQ( storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename1 ), 0 );
        ASSERT_EQ(
            storage.getSize( DataType::PAYLOAD_METADATA ),
            30 ); // 30 is a size for a default empty JSON metadata scheme, containing the version and the empty array

        int ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
