// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CacheAndPersist.h"
#include <functional>
#include <gtest/gtest.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

using namespace Aws::IoTFleetWise::Platform::Linux::PersistencyManagement;

// Unit Tests for the collectionScheme persistency
TEST( CacheAndPersistTest, testCollectionSchemePersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );

        ASSERT_TRUE( storage.init() );
        // Delete any existing contents
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );

        // create a test obj
        std::string testString = "hello CollectionScheme";
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
    }
}

// Check for the memory full condition
TEST( CacheAndPersistTest, testMemoryFull )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 2 );

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
    }
}

// Check for the invalid data type condition
TEST( CacheAndPersistTest, testInvalidDataType )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );

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
    }
}

// Unit Tests for the DM persistency
TEST( CacheAndPersistTest, testDecoderManifestPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );
        ASSERT_TRUE( storage.init() );
        // create a test obj
        std::string testString = "hello Decoder Manifest";
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
    }
}

// Tests writing multiple collectionSchemes, checks if it gets overwritten
TEST( CacheAndPersistTest, testCollectionSchemeListOverwrite )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );

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

        size_t size2 = testString2.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString2.c_str() ),
                                  size2,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size2 );

        size_t size3 = testString3.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString3.c_str() ),
                                  size3,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size3 );
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
    }
}

// Tests reading an empty file
TEST( CacheAndPersistTest, testReadEmptyFile )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );

        ASSERT_TRUE( storage.init() );

        std::string testString = "Empty File Test case";
        size_t size = testString.size();

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                                  size,
                                  DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::COLLECTION_SCHEME_LIST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

        ASSERT_EQ( storage.read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.erase( DataType::COLLECTION_SCHEME_LIST ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, DataType::COLLECTION_SCHEME_LIST ), ErrorCode::EMPTY );
    }
}

// Test Data Persistency
TEST( CacheAndPersistTest, testDataPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );

        ASSERT_TRUE( storage.init() );
        // Delete any existing contents
        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::SUCCESS );

        // create a test obj
        std::string testString = "Store this data - 1";
        size_t size = testString.size();
        std::cout << "String size: " << size << std::endl;

        ASSERT_EQ( storage.write(
                       reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DataType::EDGE_TO_CLOUD_PAYLOAD ),
                   ErrorCode::SUCCESS );

        size_t readSize = storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD );
        std::cout << "testDataPersistency::readSize: " << readSize << std::endl;

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[readSize]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), readSize, DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::SUCCESS );

        std::string out( reinterpret_cast<const char *>( readBufPtr.get() ), readSize );
        ASSERT_STREQ( out.c_str(), testString.c_str() );

        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD ), 0 );
    }
}

// Test multiple events being logged while the car is offline
TEST( CacheAndPersistTest, testMultipleEventDataPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::cout << " File being saved here: " << std::string( buffer ) << std::endl;
        CacheAndPersist storage( std::string( buffer ), 131072 );

        ASSERT_TRUE( storage.init() );
        // Delete any existing contents
        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::SUCCESS );

        // create a test obj
        std::string testString1 = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
        size_t size1 = testString1.size();
        std::cout << "String size: " << size1 << std::endl;

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString1.c_str() ),
                                  size1,
                                  DataType::EDGE_TO_CLOUD_PAYLOAD ),
                   ErrorCode::SUCCESS );

        std::string testString2 = "34567089!@#$%^&*()_+?><";
        size_t size2 = testString2.size();
        std::cout << "String size: " << size2 << std::endl;

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString2.c_str() ),
                                  size2,
                                  DataType::EDGE_TO_CLOUD_PAYLOAD ),
                   ErrorCode::SUCCESS );

        size_t readSize = storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[readSize]() );

        ASSERT_EQ( storage.read( readBufPtr.get(), readSize, DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::SUCCESS );

        // Verify if the size of the data written is same as size of the data read back
        // contents of the file need parsing and are being tested at a higher level test file
        ASSERT_EQ( readSize, size1 + size2 );

        ASSERT_EQ( storage.erase( DataType::EDGE_TO_CLOUD_PAYLOAD ), ErrorCode::SUCCESS );
        ASSERT_EQ( storage.getSize( DataType::EDGE_TO_CLOUD_PAYLOAD ), 0 );
    }
}
