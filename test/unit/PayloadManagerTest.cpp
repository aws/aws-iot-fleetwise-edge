// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadManager.h"
#include "CacheAndPersist.h"
#include "Testing.h"
#include <boost/filesystem.hpp>
#include <cstdint>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

TEST( PayloadManagerTest, TestNoConnectionDataPersistency )
{
    auto persistencyPtr = createCacheAndPersist();

    PayloadManager testSend( persistencyPtr );

    std::string testData1 = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
    testData1 +=
        '\0'; // make sure compression can handle null characters and does not stop after the first null character
    testData1 += "testproto";

    const uint8_t *stringData1 = reinterpret_cast<const uint8_t *>( testData1.data() );

    Json::Value metadata;
    metadata["someOtherField"] = 13;
    std::string filename = "12345_09876.bin";

    ASSERT_TRUE( testSend.storeData( stringData1, testData1.size(), metadata, filename ) );

    Json::Value files;
    ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::SUCCESS );
    ASSERT_EQ( files[0]["someOtherField"].asUInt(), 13 );

    std::vector<uint8_t> payload1( testData1.size() );
    ASSERT_EQ( testSend.retrievePayload( payload1.data(), testData1.size(), filename ), ErrorCode::SUCCESS );
    ASSERT_EQ( testData1.size(), payload1.size() );
    ASSERT_EQ( std::string( payload1.begin(), payload1.end() ), testData1 );

    persistencyPtr->erase( DataType::PAYLOAD_METADATA );
}

TEST( PayloadManagerTest, TestNoConnectionDataPersistencyWithStreambuf )
{
    auto persistencyPtr = createCacheAndPersist();

    PayloadManager testSend( persistencyPtr );

    std::string testData = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
    testData +=
        '\0'; // make sure compression can handle null characters and does not stop after the first null character
    testData += "testproto";

    std::stringstream stream( testData );

    Json::Value metadata;
    metadata["someOtherField"] = 13;
    std::string filename = "12345_09876.bin";

    ASSERT_TRUE( testSend.storeData( *stream.rdbuf(), metadata, filename ) );

    Json::Value files;
    ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::SUCCESS );
    // For now stream support is partial, we don't add the saved files to the metadata yet
    ASSERT_EQ( files.size(), 0 );

    std::vector<uint8_t> payload( testData.size() );
    ASSERT_EQ( testSend.retrievePayload( payload.data(), testData.size(), filename ), ErrorCode::SUCCESS );
    ASSERT_EQ( std::string( payload.begin(), payload.end() ), testData );
}

TEST( PayloadManagerTest, TestNoCacheAndPersistModuleWithStreambuf )
{
    PayloadManager testSend( nullptr );

    std::string testData = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
    std::stringstream stream( testData );

    Json::Value metadata;
    metadata["someOtherField"] = 13;
    std::string filename = "12345_09876.bin";

    ASSERT_FALSE( testSend.storeData( *stream.rdbuf(), metadata, filename ) );
    ASSERT_EQ( testSend.retrievePayload( nullptr, 0, filename ), ErrorCode::INVALID_DATA );
    Json::Value files;
    ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::INVALID_DATA );
}

TEST( PayloadManagerTest, TestNoCacheAndPersistModule )
{
    PayloadManager testSend( nullptr );

    std::string testData = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";

    const uint8_t *stringData = reinterpret_cast<const uint8_t *>( testData.data() );

    Json::Value metadata;
    metadata["someOtherField"] = 13;

    std::string filename = "12345_09876.bin";
    ASSERT_FALSE( testSend.storeData( stringData, testData.size(), metadata, filename ) );
    ASSERT_EQ( testSend.retrievePayload( nullptr, 0, filename ), ErrorCode::INVALID_DATA );
    Json::Value files;
    ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::INVALID_DATA );
}

TEST( PayloadManagerTest, TestEmptyBuffer )
{
    auto persistencyPtr = createCacheAndPersist();
    PayloadManager testSend( persistencyPtr );

    Json::Value metadata;
    std::string filename = "12345_09876.bin";

    ASSERT_FALSE( testSend.storeData( nullptr, 0, metadata, filename ) );
    ASSERT_EQ( testSend.retrievePayload( nullptr, 0, filename ), ErrorCode::INVALID_DATA );
}

TEST( PayloadManagerTest, TestFailToAddMetadata )
{
    auto persistencyPtr = std::make_shared<CacheAndPersist>( getTempDir().string(), 120 );

    PayloadManager testSend( persistencyPtr );

    std::string testData1 = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
    const uint8_t *stringData1 = reinterpret_cast<const uint8_t *>( testData1.data() );

    Json::Value metadata;
    metadata["someOtherField"] = 13;
    ASSERT_FALSE( testSend.storeData( stringData1, testData1.size(), metadata, "12345_09876.bin" ) );

    std::string filename = "12345_09876.bin";

    Json::Value files;
    std::vector<uint8_t> payload1( testData1.size() );
    ASSERT_EQ( testSend.retrievePayloadMetadata( files ), ErrorCode::SUCCESS );
    ASSERT_EQ( testSend.retrievePayload( payload1.data(), payload1.size(), filename ), ErrorCode::EMPTY );
    ASSERT_EQ( persistencyPtr->getSize( DataType::EDGE_TO_CLOUD_PAYLOAD, filename ), 0 );
}

} // namespace IoTFleetWise
} // namespace Aws
