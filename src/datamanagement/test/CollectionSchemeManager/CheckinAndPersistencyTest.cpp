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

#include "CollectionSchemeManagerMock.h"
#include "CollectionSchemeManagerTest.h"
#include <utility>

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

/** @brief
 * This test validates the retry logic of sending checkin messages.
 * This is a non-connected test case ( no underlying MQTT Connection is created).
 * This test validates that the scheduling logic is applied cyclicly.
 */
TEST( CollectionSchemeManagerTest2, checkInScheduleLogicTest )
{
    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    std::string strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";

    // create empty collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionScheme> collectionScheme2 = std::make_shared<mockCollectionScheme>();
    std::map<std::string, ICollectionSchemePtr> mapEmpty;
    std::map<std::string, ICollectionSchemePtr> mapEnable = {
        { strCollectionSchemeIDCollectionScheme1, collectionScheme1 },
        { strCollectionSchemeIDCollectionScheme2, collectionScheme2 } };

    // setup maps
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1, mapEnable, mapEmpty );
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePointInMsec currTime = testClock->timeSinceEpochMs();
    // create mTimeLine
    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> testTimeLine;
    TimeData dataPair = std::make_pair( currTime, "Checkin" );
    testTimeLine.push( dataPair );
    // test code
    CANInterfaceIDTranslator canIDTranslator;
    gmocktest.init( 200, nullptr, canIDTranslator );
    gmocktest.setTimeLine( testTimeLine );
    gmocktest.checkTimeLine( currTime );
    // We should have popped one item from the TimeLine, but also scheduled another one for the next cycle.
    TimeData topPair = gmocktest.getTimeLine().top();
    ASSERT_EQ( topPair.first, currTime + 200 );
    ASSERT_EQ( topPair.second, "Checkin" );
}

/** @brief
 * This test validates the usage of the store API of the persistency module.
 */
TEST( CollectionSchemeManagerTest2, storeTest )
{
    std::string strDecoderManifestID1 = "DM1";
    // create testPersistency
    std::shared_ptr<mockCacheAndPersist> testPersistency = std::make_shared<mockCacheAndPersist>();
    // build decodermanifest testDM1
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<uint8_t> dataPL = { '1', '2', '3', '4' };
    std::vector<uint8_t> dataDM = { '1', '2', '3', '4' };
    std::vector<uint8_t> dataEmpty;

    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1 );
    EXPECT_CALL( *testPL, getData() )
        .WillOnce( ReturnRef( dataEmpty ) )
        .WillOnce( ReturnRef( dataPL ) )
        .WillOnce( ReturnRef( dataPL ) );
    EXPECT_CALL( *testDM1, getData() )
        .WillOnce( ReturnRef( dataEmpty ) )
        .WillOnce( ReturnRef( dataDM ) )
        .WillOnce( ReturnRef( dataDM ) );
    EXPECT_CALL( *testPersistency, write( _, _, COLLECTION_SCHEME_LIST ) )
        .WillOnce( Return( SUCCESS ) )
        .WillOnce( Return( MEMORY_FULL ) );
    EXPECT_CALL( *testPersistency, write( _, _, DECODER_MANIFEST ) )
        .WillOnce( Return( SUCCESS ) )
        .WillOnce( Return( MEMORY_FULL ) );

    // all nullptr
    gmocktest.store( COLLECTION_SCHEME_LIST );
    // collectionSchemeList nullptr
    gmocktest.setCollectionSchemePersistency( testPersistency );
    gmocktest.store( COLLECTION_SCHEME_LIST );
    // DM nullptr
    gmocktest.store( DECODER_MANIFEST );
    // wrong data type
    gmocktest.store( EDGE_TO_CLOUD_PAYLOAD );
    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.setDecoderManifest( testDM1 );
    // getData() return empty
    gmocktest.store( COLLECTION_SCHEME_LIST );
    gmocktest.store( DECODER_MANIFEST );
    // getData() return >0, write returns success
    gmocktest.store( COLLECTION_SCHEME_LIST );
    gmocktest.store( DECODER_MANIFEST );
    // getData() return >0, write returns memory_full
    gmocktest.store( COLLECTION_SCHEME_LIST );
    gmocktest.store( DECODER_MANIFEST );
}

/** @brief
 * This test validates the usage of the retrieve API of the persistency module.
 */
TEST( CollectionSchemeManagerTest2, retrieveTest )
{
    std::string strDecoderManifestID1 = "DM1";
    // create testPersistency
    std::shared_ptr<mockCacheAndPersist> testPersistency = std::make_shared<mockCacheAndPersist>();
    // build decodermanifest testDM1
    std::shared_ptr<mockDecoderManifest> testDM = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();

    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1 );
    EXPECT_CALL( *testPersistency, getSize( COLLECTION_SCHEME_LIST ) )
        .WillOnce( Return( 0 ) )
        .WillOnce( Return( 100 ) )
        .WillOnce( Return( 100 ) );
    EXPECT_CALL( *testPersistency, getSize( DECODER_MANIFEST ) )
        .WillOnce( Return( 0 ) )
        .WillOnce( Return( 100 ) )
        .WillOnce( Return( 100 ) );
    EXPECT_CALL( *testPersistency, read( _, _, COLLECTION_SCHEME_LIST ) )
        .WillOnce( Return( FILESYSTEM_ERROR ) )
        .WillOnce( Return( SUCCESS ) );
    EXPECT_CALL( *testPersistency, read( _, _, DECODER_MANIFEST ) )
        .WillOnce( Return( FILESYSTEM_ERROR ) )
        .WillOnce( Return( SUCCESS ) );
    EXPECT_CALL( *testPL, copyData( _, 100 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( *testDM, copyData( _, 100 ) ).WillOnce( Return( true ) );

    // all nullptr
    ASSERT_FALSE( gmocktest.retrieve( COLLECTION_SCHEME_LIST ) );
    // wrong datatype
    gmocktest.setCollectionSchemePersistency( testPersistency );
    ASSERT_FALSE( gmocktest.retrieve( EDGE_TO_CLOUD_PAYLOAD ) );
    // zero size
    ASSERT_FALSE( gmocktest.retrieve( COLLECTION_SCHEME_LIST ) );
    ASSERT_FALSE( gmocktest.retrieve( DECODER_MANIFEST ) );

    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.setDecoderManifest( testDM );
    // read failure
    ASSERT_FALSE( gmocktest.retrieve( COLLECTION_SCHEME_LIST ) );
    ASSERT_FALSE( gmocktest.getmProcessCollectionScheme() );
    ASSERT_FALSE( gmocktest.retrieve( DECODER_MANIFEST ) );
    ASSERT_FALSE( gmocktest.getmProcessDecoderManifest() );
    // read success collectionSchemeList valid, dm valid
    ASSERT_TRUE( gmocktest.retrieve( COLLECTION_SCHEME_LIST ) );
    ASSERT_TRUE( gmocktest.getmProcessCollectionScheme() );
    ASSERT_TRUE( gmocktest.retrieve( DECODER_MANIFEST ) );
    ASSERT_TRUE( gmocktest.getmProcessDecoderManifest() );
}

/** @brief
 * This test validates the usage of the store/retrieve APIs combined of the persistency module.
 */
TEST( CollectionSchemeManagerTest2, StoreAndRetrieve )
{
    int ret = std::system( "mkdir ./testPersist" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    std::shared_ptr<ICacheAndPersist> testPersistency = std::make_shared<CacheAndPersist>( "./testPersist", 4096 );
    std::string dataPL =
        "if the destructor for an automatic object is explicitly invoked, and the block is subsequently left in a "
        "manner that would ordinarily invoke implicit destruction of the object, the behavior is undefined";
    std::string dataDM =
        "explicit calls of destructors are rarely needed. One use of such calls is for objects placed at specific "
        "addresses using a placement new-expression. Such use of explicit placement and destruction of objects can be "
        "necessary to cope with dedicated hardware resources and for writing memory management facilities.";
    size_t sizePL = dataPL.length();
    size_t sizeDM = dataDM.length();

    CollectionSchemeManagerTest testCollectionSchemeManager;
    testCollectionSchemeManager.setCollectionSchemePersistency( testPersistency );
    std::vector<ICollectionSchemePtr> emptyCP;
    ICollectionSchemeListPtr storePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    storePL->copyData( reinterpret_cast<const uint8_t *>( dataPL.c_str() ), sizePL );
    IDecoderManifestPtr storeDM = std::make_shared<IDecoderManifestTest>( "DM" );
    storeDM->copyData( reinterpret_cast<const uint8_t *>( dataDM.c_str() ), sizeDM );
    testCollectionSchemeManager.setCollectionSchemeList( storePL );
    testCollectionSchemeManager.setDecoderManifest( storeDM );
    testCollectionSchemeManager.store( COLLECTION_SCHEME_LIST );
    testCollectionSchemeManager.store( DECODER_MANIFEST );

    ret = std::system( "ls -l ./testPersist >test.txt" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );
    std::cout << std::ifstream( "test.txt" ).rdbuf();

    ICollectionSchemeListPtr retrievePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    IDecoderManifestPtr retrieveDM = std::make_shared<IDecoderManifestTest>( "DM" );
    testCollectionSchemeManager.setCollectionSchemeList( retrievePL );
    testCollectionSchemeManager.setDecoderManifest( retrieveDM );
    testCollectionSchemeManager.retrieve( COLLECTION_SCHEME_LIST );
    testCollectionSchemeManager.retrieve( DECODER_MANIFEST );

    std::vector<uint8_t> orgData = storePL->getData();
    std::vector<uint8_t> retData = retrievePL->getData();
    ASSERT_EQ( orgData, retData );
    orgData = storeDM->getData();
    retData = retrieveDM->getData();
    ASSERT_EQ( orgData, retData );
    ret = std::system( "rm -rf ./testPersist" );
    ASSERT_FALSE( WIFEXITED( ret ) == 0 );
}
