/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "CacheAndPersist.h"
#include <functional>
#include <gtest/gtest.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

using namespace Aws::IoTFleetWise::Platform::PersistencyManagement;

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
        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );

        // create a test obj
        std::string testString = "hello CollectionScheme";
        size_t size = testString.size();

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, COLLECTION_SCHEME_LIST ),
            SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, COLLECTION_SCHEME_LIST ), SUCCESS );

        std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );

        std::cout << "File contents: " << out << std::endl;
        ASSERT_STREQ( out.c_str(), testString.c_str() );
        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), 0 );
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
        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );

        // create a test obj
        std::string testString = "test123";

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ),
                                  testString.size(),
                                  COLLECTION_SCHEME_LIST ),
                   MEMORY_FULL );
        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), 0 );
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
        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DEFAULT_DATA_TYPE ),
                   INVALID_DATATYPE );

        // Reading back the data
        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

        ASSERT_EQ( storage.getSize( DEFAULT_DATA_TYPE ), INVALID_FILE_SIZE );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, DEFAULT_DATA_TYPE ), INVALID_DATATYPE );
        ASSERT_EQ( storage.erase( DEFAULT_DATA_TYPE ), INVALID_DATATYPE );
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

        ASSERT_EQ( storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, DECODER_MANIFEST ),
                   SUCCESS );
        ASSERT_EQ( storage.getSize( DECODER_MANIFEST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, DECODER_MANIFEST ), SUCCESS );
        std::string out( reinterpret_cast<char *>( readBufPtr.get() ), size );
        std::cout << "File contents: " << out << std::endl;
        ASSERT_STREQ( out.c_str(), testString.c_str() );
        ASSERT_EQ( storage.erase( DECODER_MANIFEST ), SUCCESS );
        ASSERT_EQ( storage.getSize( DECODER_MANIFEST ), 0 );
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

        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );

        // create a test obj
        std::string testString1 = "CollectionScheme 1234";
        std::string testString2 = "CollectionScheme 5";
        std::string testString3 = "CollectionScheme 678";

        size_t size1 = testString1.size();
        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString1.c_str() ), size1, COLLECTION_SCHEME_LIST ),
            SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), size1 );

        size_t size2 = testString2.size();

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString2.c_str() ), size2, COLLECTION_SCHEME_LIST ),
            SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), size2 );

        size_t size3 = testString3.size();

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString3.c_str() ), size3, COLLECTION_SCHEME_LIST ),
            SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), size3 );
        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );
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

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, COLLECTION_SCHEME_LIST ),
            SUCCESS );
        ASSERT_EQ( storage.getSize( COLLECTION_SCHEME_LIST ), size );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[size]() );

        ASSERT_EQ( storage.read( readBufPtr.get(), size, COLLECTION_SCHEME_LIST ), SUCCESS );
        ASSERT_EQ( storage.erase( COLLECTION_SCHEME_LIST ), SUCCESS );
        ASSERT_EQ( storage.read( readBufPtr.get(), size, COLLECTION_SCHEME_LIST ), EMPTY );
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
        ASSERT_EQ( storage.erase( EDGE_TO_CLOUD_PAYLOAD ), SUCCESS );

        // create a test obj
        std::string testString = "Store this data - 1";
        size_t size = testString.size();
        std::cout << "String size: " << size << std::endl;

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString.c_str() ), size, EDGE_TO_CLOUD_PAYLOAD ),
            SUCCESS );

        size_t readSize = storage.getSize( EDGE_TO_CLOUD_PAYLOAD );
        std::cout << "testDataPersistency::readSize: " << readSize << std::endl;

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[readSize]() );
        ASSERT_EQ( storage.read( readBufPtr.get(), readSize, EDGE_TO_CLOUD_PAYLOAD ), SUCCESS );

        std::string out( reinterpret_cast<const char *>( readBufPtr.get() ), readSize );
        ASSERT_STREQ( out.c_str(), testString.c_str() );

        ASSERT_EQ( storage.erase( EDGE_TO_CLOUD_PAYLOAD ), SUCCESS );
        ASSERT_EQ( storage.getSize( EDGE_TO_CLOUD_PAYLOAD ), 0 );
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
        ASSERT_EQ( storage.erase( EDGE_TO_CLOUD_PAYLOAD ), SUCCESS );

        // create a test obj
        std::string testString1 = "abcdefjh!24$iklmnop!24$3@qrstuvwxyz";
        size_t size1 = testString1.size();
        std::cout << "String size: " << size1 << std::endl;

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString1.c_str() ), size1, EDGE_TO_CLOUD_PAYLOAD ),
            SUCCESS );

        std::string testString2 = "34567089!@#$%^&*()_+?><";
        size_t size2 = testString2.size();
        std::cout << "String size: " << size2 << std::endl;

        ASSERT_EQ(
            storage.write( reinterpret_cast<const uint8_t *>( testString2.c_str() ), size2, EDGE_TO_CLOUD_PAYLOAD ),
            SUCCESS );

        size_t readSize = storage.getSize( EDGE_TO_CLOUD_PAYLOAD );

        std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[readSize]() );

        ASSERT_EQ( storage.read( readBufPtr.get(), readSize, EDGE_TO_CLOUD_PAYLOAD ), SUCCESS );

        // Verify if the size of the data written is same as size of the data read back
        // contents of the file need parsing and are being tested at a higher level test file
        ASSERT_EQ( readSize, size1 + size2 );

        ASSERT_EQ( storage.erase( EDGE_TO_CLOUD_PAYLOAD ), SUCCESS );
        ASSERT_EQ( storage.getSize( EDGE_TO_CLOUD_PAYLOAD ), 0 );
    }
}