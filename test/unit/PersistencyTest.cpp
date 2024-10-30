// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "CheckinSender.h"
#include "CollectionSchemeManagerMock.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include <cstdlib>
#include <fstream> // IWYU pragma: keep
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

/** @brief
 * This test validates the usage of the store API of the persistency module.
 */
TEST( PersistencyTest, storeTest )
{
    SyncID strDecoderManifestID1 = "DM1";
    // create testPersistency
    std::shared_ptr<mockCacheAndPersist> testPersistency = std::make_shared<mockCacheAndPersist>();
    // build decodermanifest testDM1
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<uint8_t> dataPL = { '1', '2', '3', '4' };
    std::vector<uint8_t> dataDM = { '1', '2', '3', '4' };
    std::vector<uint8_t> dataEmpty;

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), strDecoderManifestID1 );
    EXPECT_CALL( *testPL, getData() )
        .WillOnce( ReturnRef( dataEmpty ) )
        .WillOnce( ReturnRef( dataPL ) )
        .WillOnce( ReturnRef( dataPL ) );
    EXPECT_CALL( *testDM1, getData() )
        .WillOnce( ReturnRef( dataEmpty ) )
        .WillOnce( ReturnRef( dataDM ) )
        .WillOnce( ReturnRef( dataDM ) );
    EXPECT_CALL( *testPersistency, write( _, _, DataType::COLLECTION_SCHEME_LIST, _ ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) )
        .WillOnce( Return( ErrorCode::MEMORY_FULL ) );
    EXPECT_CALL( *testPersistency, write( _, _, DataType::DECODER_MANIFEST, _ ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) )
        .WillOnce( Return( ErrorCode::MEMORY_FULL ) );

    // all nullptr
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    // collectionSchemeList nullptr
    gmocktest.setCollectionSchemePersistency( testPersistency );
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    // DM nullptr
    gmocktest.store( DataType::DECODER_MANIFEST );
    // wrong data type
    gmocktest.store( DataType::EDGE_TO_CLOUD_PAYLOAD );
    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.setDecoderManifest( testDM1 );
    // getData() return empty
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    gmocktest.store( DataType::DECODER_MANIFEST );
    // getData() return >0, write returns success
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    gmocktest.store( DataType::DECODER_MANIFEST );
    // getData() return >0, write returns memory_full
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    gmocktest.store( DataType::DECODER_MANIFEST );
}

/** @brief
 * This test validates the usage of the retrieve API of the persistency module.
 */
TEST( PersistencyTest, retrieveTest )
{
    SyncID strDecoderManifestID1 = "DM1";
    // create testPersistency
    std::shared_ptr<mockCacheAndPersist> testPersistency = std::make_shared<mockCacheAndPersist>();
    // build decodermanifest testDM1
    std::shared_ptr<mockDecoderManifest> testDM = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), strDecoderManifestID1 );
    EXPECT_CALL( *testPersistency, getSize( DataType::COLLECTION_SCHEME_LIST, _ ) )
        .WillOnce( Return( 0 ) )
        .WillOnce( Return( 100 ) )
        .WillOnce( Return( 100 ) );
    EXPECT_CALL( *testPersistency, getSize( DataType::DECODER_MANIFEST, _ ) )
        .WillOnce( Return( 0 ) )
        .WillOnce( Return( 100 ) )
        .WillOnce( Return( 100 ) );
    EXPECT_CALL( *testPersistency, read( _, _, DataType::COLLECTION_SCHEME_LIST, _ ) )
        .WillOnce( Return( ErrorCode::FILESYSTEM_ERROR ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) );
    EXPECT_CALL( *testPersistency, read( _, _, DataType::DECODER_MANIFEST, _ ) )
        .WillOnce( Return( ErrorCode::FILESYSTEM_ERROR ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) );
    EXPECT_CALL( *testPL, copyData( _, 100 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( *testDM, copyData( _, 100 ) ).WillOnce( Return( true ) );

    // all nullptr
    ASSERT_FALSE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    // wrong datatype
    gmocktest.setCollectionSchemePersistency( testPersistency );
    ASSERT_FALSE( gmocktest.retrieve( DataType::EDGE_TO_CLOUD_PAYLOAD ) );
    // zero size
    ASSERT_FALSE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    ASSERT_FALSE( gmocktest.retrieve( DataType::DECODER_MANIFEST ) );

    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.setDecoderManifest( testDM );
    // read failure
    ASSERT_FALSE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    ASSERT_FALSE( gmocktest.getmProcessCollectionScheme() );
    ASSERT_FALSE( gmocktest.retrieve( DataType::DECODER_MANIFEST ) );
    ASSERT_FALSE( gmocktest.getmProcessDecoderManifest() );
    // read success collectionSchemeList valid, dm valid
    ASSERT_TRUE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    ASSERT_TRUE( gmocktest.getmProcessCollectionScheme() );
    ASSERT_TRUE( gmocktest.retrieve( DataType::DECODER_MANIFEST ) );
    ASSERT_TRUE( gmocktest.getmProcessDecoderManifest() );
}

/** @brief
 * This test validates the usage of the store/retrieve APIs combined of the persistency module.
 */
TEST( PersistencyTest, StoreAndRetrieve )
{
    int ret = std::system( "mkdir ./testPersist" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    std::shared_ptr<CacheAndPersist> testPersistency = std::make_shared<CacheAndPersist>( "./testPersist", 4096 );
    ASSERT_TRUE( testPersistency->init() );
    std::string dataPL =
        "if the destructor for an automatic object is explicitly invoked, and the block is subsequently left in a "
        "manner that would ordinarily invoke implicit destruction of the object, the behavior is undefined";
    std::string dataDM =
        "explicit calls of destructors are rarely needed. One use of such calls is for objects placed at specific "
        "addresses using a placement new-expression. Such use of explicit placement and destruction of objects can be "
        "necessary to cope with dedicated hardware resources and for writing memory management facilities.";
    size_t sizePL = dataPL.length();
    size_t sizeDM = dataDM.length();

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper testCollectionSchemeManager(
        testPersistency, canIDTranslator, std::make_shared<CheckinSender>( nullptr ) );
    std::vector<ICollectionSchemePtr> emptyCP;
    ICollectionSchemeListPtr storePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    storePL->copyData( reinterpret_cast<const uint8_t *>( dataPL.c_str() ), sizePL );
    IDecoderManifestPtr storeDM = std::make_shared<IDecoderManifestTest>( "DM" );
    storeDM->copyData( reinterpret_cast<const uint8_t *>( dataDM.c_str() ), sizeDM );
    testCollectionSchemeManager.setCollectionSchemeList( storePL );
    testCollectionSchemeManager.setDecoderManifest( storeDM );
    testCollectionSchemeManager.store( DataType::COLLECTION_SCHEME_LIST );
    testCollectionSchemeManager.store( DataType::DECODER_MANIFEST );

    ret = std::system( "ls -l ./testPersist >test.txt" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    std::cout << std::ifstream( "test.txt" ).rdbuf();

    ICollectionSchemeListPtr retrievePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    IDecoderManifestPtr retrieveDM = std::make_shared<IDecoderManifestTest>( "DM" );
    testCollectionSchemeManager.setCollectionSchemeList( retrievePL );
    testCollectionSchemeManager.setDecoderManifest( retrieveDM );
    testCollectionSchemeManager.retrieve( DataType::COLLECTION_SCHEME_LIST );
    testCollectionSchemeManager.retrieve( DataType::DECODER_MANIFEST );

    std::vector<uint8_t> orgData = storePL->getData();
    std::vector<uint8_t> retData = retrievePL->getData();
    ASSERT_EQ( orgData, retData );
    orgData = storeDM->getData();
    retData = retrieveDM->getData();
    ASSERT_EQ( orgData, retData );
    ret = std::system( "rm -rf ./testPersist" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );
}

} // namespace IoTFleetWise
} // namespace Aws
