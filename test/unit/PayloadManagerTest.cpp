// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadManager.h"
#include "CacheAndPersist.h"
#include "ISender.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

TEST( PayloadManagerTest, TestNoConnectionDataPersistency )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->erase( DataType::PAYLOAD_METADATA );
        persistencyPtr->init();
        PayloadManager testSend( persistencyPtr );

        std::string testData1 = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
        testData1 +=
            '\0'; // make sure compression can handle null characters and does not stop after the first null character
        testData1 += "testproto";

        const uint8_t *stringData1 = reinterpret_cast<const uint8_t *>( testData1.data() );

        CollectionSchemeParams collectionSchemeParams1;
        collectionSchemeParams1.compression = false;
        collectionSchemeParams1.eventID = 123456;
        collectionSchemeParams1.triggerTime = 123456;

        CollectionSchemeParams collectionSchemeParams2;
        collectionSchemeParams2.compression = false;
        collectionSchemeParams2.eventID = 678910;
        collectionSchemeParams2.triggerTime = 678910;

        ASSERT_EQ( testSend.storeData( stringData1, testData1.size(), collectionSchemeParams1 ), true );

        std::string filename;

        Json::Value files;
        ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::SUCCESS );
        ASSERT_EQ( files[0]["filename"],
                   filename + std::to_string( collectionSchemeParams1.eventID ) + "-" +
                       std::to_string( collectionSchemeParams1.triggerTime ) + ".bin" );
        ASSERT_EQ( files[0]["payloadSize"].asUInt(), testData1.size() );
        ASSERT_EQ( files[0]["compressionRequired"], false );

        filename = files[0]["filename"].asString();
        std::vector<uint8_t> payload1( files[0]["payloadSize"].asUInt() );
        ASSERT_EQ( testSend.retrievePayload( payload1.data(), files[0]["payloadSize"].asUInt(), filename ),
                   ErrorCode::SUCCESS );
        ASSERT_EQ( testData1.size(), payload1.size() );
        ASSERT_TRUE( std::equal( testData1.begin(), testData1.begin() + testData1.size(), payload1.begin() ) );

        persistencyPtr->erase( DataType::PAYLOAD_METADATA );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

TEST( PayloadManagerTest, TestNoCacheAndPersistModule )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        PayloadManager testSend( nullptr );

        std::string testData = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";

        const uint8_t *stringData = reinterpret_cast<const uint8_t *>( testData.data() );

        CollectionSchemeParams collectionSchemeParams;
        collectionSchemeParams.compression = false;
        collectionSchemeParams.eventID = 123456;
        collectionSchemeParams.triggerTime = 123456;

        std::string filename;
        ASSERT_EQ( testSend.storeData( stringData, testData.size(), collectionSchemeParams ), false );
        ASSERT_EQ( testSend.retrievePayload( nullptr, 0, filename ), ErrorCode::INVALID_DATA );
        Json::Value files;
        ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::INVALID_DATA );
    }
}

TEST( PayloadManagerTest, TestEmptyBuffer )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->erase( DataType::PAYLOAD_METADATA );
        persistencyPtr->init();
        PayloadManager testSend( persistencyPtr );

        CollectionSchemeParams collectionSchemeParams1;
        collectionSchemeParams1.compression = false;
        collectionSchemeParams1.eventID = 123456;
        collectionSchemeParams1.triggerTime = 123456;

        std::string filename;
        ASSERT_EQ( testSend.storeData( nullptr, 0, collectionSchemeParams1 ), false );
        ASSERT_EQ( testSend.retrievePayload( nullptr, 0, filename ), ErrorCode::INVALID_DATA );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

TEST( PayloadManagerTest, TestFailToAddMetadata )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 120 );
        persistencyPtr->init();
        PayloadManager testSend( persistencyPtr );

        std::string testData1 = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
        const uint8_t *stringData1 = reinterpret_cast<const uint8_t *>( testData1.data() );

        CollectionSchemeParams collectionSchemeParams1;
        collectionSchemeParams1.compression = false;
        collectionSchemeParams1.eventID = 123456;
        collectionSchemeParams1.triggerTime = 123456;
        ASSERT_EQ( testSend.storeData( stringData1, testData1.size(), collectionSchemeParams1 ), false );

        std::string filename = std::to_string( collectionSchemeParams1.eventID ) + "-" +
                               std::to_string( collectionSchemeParams1.triggerTime ) + ".bin";

        Json::Value files;
        std::vector<uint8_t> payload1( testData1.size() );
        ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::SUCCESS );
        ASSERT_EQ( testSend.retrievePayload( payload1.data(), payload1.size(), filename ), ErrorCode::EMPTY );
        ASSERT_EQ( persistencyPtr->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), 0 );

        ret = std::system( "rm -rf ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
