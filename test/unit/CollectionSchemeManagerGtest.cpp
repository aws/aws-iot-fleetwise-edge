// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CANInterfaceIDTranslator.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionSchemeManagerMock.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include "Testing.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

/** @brief
 * This test validates the life cycle of the Collection Schemes as they get
 * build by the Collection Scheme manager.
 */
TEST( CollectionSchemeManagerGtest, RebuildUpdateAndTimeLineTest )
{

    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    std::string strDecoderManifestIDCollectionScheme1 = "DM1";
    std::string strDecoderManifestIDCollectionScheme2 = "DM2";
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startIdleTime = currTime.systemTimeMs + 10;
    Timestamp stopIdleTime = currTime.systemTimeMs + 50;

    Timestamp startEnableTime = currTime.systemTimeMs - 50;
    Timestamp stopEnableTime = currTime.systemTimeMs + 10;
    Timestamp stopDisableTime = currTime.systemTimeMs - 10;
    // build decodermanifest, collectionScheme, collectionSchemeList
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<ICollectionSchemePtr> testCP, emptyCP;
    testCP.emplace_back( collectionScheme1 );
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1 );

    EXPECT_CALL( *testPL, getCollectionSchemes() )
        .WillOnce( ReturnRef( emptyCP ) )
        .WillOnce( ReturnRef( testCP ) )
        .WillOnce( ReturnRef( testCP ) )
        .WillOnce( ReturnRef( testCP ) )
        .WillOnce( ReturnRef( testCP ) );

    EXPECT_CALL( *collectionScheme1, getDecoderManifestID() )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme2 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme2 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) );

    EXPECT_CALL( *collectionScheme1, getStartTime() )
        .WillOnce( Return( startIdleTime ) )
        .WillOnce( Return( startEnableTime ) )
        .WillOnce( Return( startEnableTime ) );

    EXPECT_CALL( *collectionScheme1, getExpiryTime() )
        .WillOnce( Return( stopIdleTime ) )
        .WillOnce( Return( stopEnableTime ) )
        .WillOnce( Return( stopDisableTime ) );

    EXPECT_CALL( *collectionScheme1, getCollectionSchemeID() )
        .WillRepeatedly( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) );

    // test code
    // 1. empty ICollectionScheme
    std::cout << COUT_GTEST_MGT << "1. Empty ICollectionCollectionScheme." << ANSI_TXT_DFT << std::endl;
    gmocktest.setCollectionSchemeList( testPL );
    ASSERT_FALSE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 2. collectionScheme does not match currDMID
    std::cout << COUT_GTEST_MGT << "2. CollectionScheme does not match current DM id." << ANSI_TXT_DFT << std::endl;
    ASSERT_FALSE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 3. DM matches, start time > currTime, add idle collectionScheme
    std::cout << COUT_GTEST_MGT << " 3. DM matches, start time > currTime, add idle collectionScheme" << ANSI_TXT_DFT
              << std::endl;
    ASSERT_FALSE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 4. DM matches, start time <= currTime && stop time > currTime add enable collectionScheme
    std::cout << COUT_GTEST_MGT
              << "4. DM matches, start time <= currTime && stop time > currTime add enable collectionScheme"
              << ANSI_TXT_DFT << std::endl;
    ASSERT_TRUE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 5. DM matches, stop time < currTime already expired
    std::cout << COUT_GTEST_MGT << "5. DM matches, stop time < currTime CollectionScheme already expired"
              << ANSI_TXT_DFT << std::endl;
    ASSERT_FALSE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
}

/** @brief
 * This test validates the life cycle of the Collection Schemes as they get
 * added/deleted by the Collection Scheme manager.
 */
TEST( CollectionSchemeManagerGtest, updateMapsandTimeLineTest_ADD_DELETE )
{
    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    std::string strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";
    std::string strCollectionSchemeIDCollectionScheme3 = "COLLECTIONSCHEME3";

    std::string strDecoderManifestIDCollectionScheme1 = "DM1";
    std::string strDecoderManifestIDCollectionScheme2 = "DM2";
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startIdleTime = currTime.systemTimeMs + 10;
    Timestamp stopIdleTime = currTime.systemTimeMs + 50;

    Timestamp startEnableTime = currTime.systemTimeMs - 50;
    Timestamp stopEnableTime = currTime.systemTimeMs + 10;

    Timestamp startBadTime = currTime.systemTimeMs - 50;
    Timestamp stopBadTime = currTime.systemTimeMs - 10;
    // build decodermanifest, collectionScheme, collectionSchemeList
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<ICollectionSchemePtr> testCP, emptyCP;
    // 3 collectionSchemes on the list, one enabled and one idle, one bad
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );

    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1 );

    EXPECT_CALL( *testPL, getCollectionSchemes() )
        .WillOnce( ReturnRef( emptyCP ) )
        // addition DM does not match
        .WillOnce( ReturnRef( testCP ) )
        // addition DM  matches
        .WillOnce( ReturnRef( testCP ) )
        // deletion
        .WillOnce( ReturnRef( emptyCP ) );

    EXPECT_CALL( *collectionScheme1, getDecoderManifestID() )
        // DM does not match
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme2 ) )
        // DM print for mismatch case
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme2 ) )
        // addition DM matches collectionScheme 1
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )

        // addition DM matches collectionScheme 2
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )

        // addition DM matches collectionScheme3
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) );

    EXPECT_CALL( *collectionScheme1, getStartTime() )
        // addition add idle collectionScheme
        .WillOnce( Return( startIdleTime ) )
        // addition add enable collectionScheme
        .WillOnce( Return( startEnableTime ) )
        // collectionScheme3 bad time setting
        .WillOnce( Return( startBadTime ) );

    EXPECT_CALL( *collectionScheme1, getExpiryTime() )
        // addition idle collectionScheme
        .WillOnce( Return( stopIdleTime ) )
        // addition enabled collectionScheme
        .WillOnce( Return( stopEnableTime ) )
        // addition bad collectionScheme
        .WillOnce( Return( stopBadTime ) );

    EXPECT_CALL( *collectionScheme1, getCollectionSchemeID() )
        // addition
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme2 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme3 ) )
        // printExistingCollectionSchemes
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme2 ) );

    // test code
    // 1. empty ICollectionScheme
    std::cout << COUT_GTEST_MGT << "1. Empty ICollectionScheme." << ANSI_TXT_DFT << std::endl;
    gmocktest.setCollectionSchemeList( testPL );
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 2. collectionScheme does not match currDMID
    std::cout << COUT_GTEST_MGT << "2. CollectionScheme does not match current DM id." << ANSI_TXT_DFT << std::endl;
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 3. DM matches, add all collectionSchemes and drop the collectionScheme with bad setting
    std::cout << COUT_GTEST_MGT
              << " 3. DM matches, add all collectionSchemes and drop the collectionScheme with bad setting"
              << ANSI_TXT_DFT << std::endl;
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
    // 4. deletion
    std::cout << COUT_GTEST_MGT << "4. Delete all" << ANSI_TXT_DFT << std::endl;
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
}

/** @brief
 * This test validates the life cycle of the Collection Schemes as they get
 * to be set to idle/enabled by the Collection Scheme manager.
 */
TEST( CollectionSchemeManagerGtest, updateMapsandTimeLineTest_IDLE_BRANCHES )
{
    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";

    std::string strDecoderManifestIDCollectionScheme1 = "DM1";
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startIdleTime = currTime.systemTimeMs + 10;
    Timestamp stopIdleTime = currTime.systemTimeMs + 50;

    Timestamp startEnableTime = currTime.systemTimeMs - 20;
    Timestamp stopEnableTime = currTime.systemTimeMs + 10;

    Timestamp start1Time = currTime.systemTimeMs + 50;
    Timestamp stop1Time = currTime.systemTimeMs - 10;

    Timestamp start2Time = currTime.systemTimeMs - 50;
    Timestamp stop2Time = currTime.systemTimeMs - 10;

    // build decodermanifest, collectionScheme, collectionSchemeList
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<ICollectionSchemePtr> testCP;
    // 3 collectionSchemes on the list, same collectionScheme but different update
    // idle->enabled, idle update and bad
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );

    // create a collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionSchemeIdle = std::make_shared<mockCollectionScheme>();
    std::map<std::string, ICollectionSchemePtr> map1 = {
        { strCollectionSchemeIDCollectionScheme1, collectionSchemeIdle } };
    std::map<std::string, ICollectionSchemePtr> mapEmpty;
    // setup idleMap
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1, mapEmpty, map1 );

    EXPECT_CALL( *testPL, getCollectionSchemes() )
        //  update Idle collectionScheme DM  matches
        .WillOnce( ReturnRef( testCP ) );

    EXPECT_CALL( *collectionScheme1, getDecoderManifestID() )
        // DM matches 3 collectionSchemes
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) );

    EXPECT_CALL( *collectionSchemeIdle, getStartTime() )
        // update bad setting
        .WillRepeatedly( Return( startIdleTime ) );
    EXPECT_CALL( *collectionSchemeIdle, getExpiryTime() )
        // update bad setting
        .WillRepeatedly( Return( stopIdleTime ) );
    using ::testing::InSequence;
    {
        InSequence s1;
        // 1st update bad setting
        EXPECT_CALL( *collectionScheme1, getStartTime() )
            // update bad setting
            .WillOnce( Return( start2Time ) );
        EXPECT_CALL( *collectionScheme1, getExpiryTime() )
            // update bad setting
            .WillOnce( Return( stop2Time ) );

        // 2nd update new idle time
        EXPECT_CALL( *collectionScheme1, getStartTime() )
            // update bad setting
            .WillOnce( Return( start1Time ) );
        EXPECT_CALL( *collectionScheme1, getExpiryTime() )
            // update bad setting
            .WillOnce( Return( stop1Time ) );

        // 3rd update move to enable
        EXPECT_CALL( *collectionScheme1, getStartTime() )
            // update bad setting
            .WillOnce( Return( startEnableTime ) );
        EXPECT_CALL( *collectionScheme1, getExpiryTime() )
            // update bad setting
            .WillOnce( Return( stopEnableTime ) );
    }

    EXPECT_CALL( *collectionScheme1, getCollectionSchemeID() )
        // addition
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        // printExistingCollectionSchemes
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) );

    // test code
    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.sendCheckin();
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
}

/** @brief
 * This test validates the life cycle of the Collection Schemes as they get
 * to be set to idle/enabled by the Collection Scheme manager.
 */
TEST( CollectionSchemeManagerGtest, updateMapsandTimeLineTest_ENABLED_BRANCHES )
{
    /*
     * unit test for function block updateMapsandTimeLine
     * branches for Enabled collectionScheme
     *
     * input: currTime, collectionScheme and decoder manifest, mEnabledCollectionSchemeMap
     * check: mEnabledCollectionSchemeMap, mIdleCollectionSchemeMap and mTimeline
     *
     * 3 updates are processed:
     * a. update enabled collectionScheme with same timing, ignored;
     * b. update with new stop time, accepted;
     * c. update enabled->disabled;
     *
     */

    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";

    std::string strDecoderManifestIDCollectionScheme1 = "DM1";
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startExpTime = currTime.systemTimeMs + 10;
    Timestamp stopExpTime = currTime.systemTimeMs;

    Timestamp startEnableTime = currTime.systemTimeMs - 20;
    Timestamp stopEnableTime = currTime.systemTimeMs + 50;

    Timestamp start1Time = currTime.systemTimeMs;
    Timestamp stop1Time = currTime.systemTimeMs + 50;

    Timestamp start2Time = currTime.systemTimeMs - 50;
    Timestamp stop2Time = currTime.systemTimeMs + 100;

    // build decodermanifest, collectionScheme, collectionSchemeList
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<ICollectionSchemePtr> testCP;
    // 3 collectionSchemes on the list, same collectionScheme but different update
    // idle->enabled, idle update and bad
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );

    // create a collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionSchemeEnabled = std::make_shared<mockCollectionScheme>();
    std::map<std::string, ICollectionSchemePtr> map1 = {
        { strCollectionSchemeIDCollectionScheme1, collectionSchemeEnabled } };
    std::map<std::string, ICollectionSchemePtr> mapEmpty;
    // setup idleMap
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1, map1, mapEmpty );

    EXPECT_CALL( *testPL, getCollectionSchemes() )
        //  update Idle collectionScheme DM  matches
        .WillOnce( ReturnRef( testCP ) );

    EXPECT_CALL( *collectionScheme1, getDecoderManifestID() )
        // DM matches 3 collectionSchemes
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strDecoderManifestIDCollectionScheme1 ) );

    EXPECT_CALL( *collectionSchemeEnabled, getStartTime() )
        // update bad setting
        .WillRepeatedly( Return( startEnableTime ) );
    EXPECT_CALL( *collectionSchemeEnabled, getExpiryTime() )
        // update bad setting
        .WillRepeatedly( Return( stopEnableTime ) );
    using ::testing::InSequence;
    {
        InSequence s1;
        // 1st update bad setting
        EXPECT_CALL( *collectionScheme1, getStartTime() )
            // update bad setting
            .WillOnce( Return( start1Time ) );
        EXPECT_CALL( *collectionScheme1, getExpiryTime() )
            // update bad setting
            .WillOnce( Return( stop1Time ) );

        // 2nd update new idle time
        EXPECT_CALL( *collectionScheme1, getStartTime() )
            // update bad setting
            .WillOnce( Return( start2Time ) );
        EXPECT_CALL( *collectionScheme1, getExpiryTime() )
            // update bad setting
            .WillOnce( Return( stop2Time ) );

        // 3rd update move to enable
        EXPECT_CALL( *collectionScheme1, getStartTime() )
            // update bad setting
            .WillOnce( Return( startExpTime ) );
        EXPECT_CALL( *collectionScheme1, getExpiryTime() )
            // update bad setting
            .WillOnce( Return( stopExpTime ) );
    }

    EXPECT_CALL( *collectionScheme1, getCollectionSchemeID() )
        // addition
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) )
        .WillOnce( ReturnRef( strCollectionSchemeIDCollectionScheme1 ) );

    // test code
    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.sendCheckin();
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
    gmocktest.sendCheckin();
}

/** @brief
 * This test validates the checkin interval when no active collection scheme is in the system
 */
TEST( CollectionSchemeManagerGtest, checkTimeLineTest_CHECKIN_UNFOUNDCOLLECTIONSCHEME )
{
    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    std::string strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";

    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();

    // create empty collectionScheme map
    std::map<std::string, ICollectionSchemePtr> mapEmpty;
    std::shared_ptr<mockCollectionScheme> collectionScheme2 = std::make_shared<mockCollectionScheme>();
    std::map<std::string, ICollectionSchemePtr> mapIdle = {
        { strCollectionSchemeIDCollectionScheme2, collectionScheme2 } };

    // setup maps
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1, mapEmpty, mapIdle );
    EXPECT_CALL( *collectionScheme2, getStartTime() ).WillOnce( Return( currTime.systemTimeMs + 20 ) );

    // create mTimeLine
    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> testTimeLine;
    TimeData dataPair = { currTime, "Checkin" };
    testTimeLine.push( dataPair );
    dataPair = { currTime, strCollectionSchemeIDCollectionScheme1 };
    testTimeLine.push( dataPair );
    dataPair = { currTime, strCollectionSchemeIDCollectionScheme2 };
    testTimeLine.push( dataPair );

    // test code
    CANInterfaceIDTranslator canIDTranslator;
    gmocktest.init( 200, nullptr, canIDTranslator );
    gmocktest.setTimeLine( testTimeLine );
    gmocktest.sendCheckin();
    // first case collectionScheme1 not found
    ASSERT_FALSE( gmocktest.checkTimeLine( currTime ) );
    // second case collectionScheme2 does not have time matched
    ASSERT_FALSE( gmocktest.checkTimeLine( currTime + 200 ) );
    // branch out startTime > currTime
    ASSERT_FALSE( gmocktest.checkTimeLine( currTime + 200 ) );
}

/** @brief
 * This test validates the checkin interval when no active collection schemes are in the system
 */
TEST( CollectionSchemeManagerGtest, checkTimeLineTest_IDLE_BRANCHES )
{
    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    std::string strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";

    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();

    // create empty collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionScheme> collectionScheme2 = std::make_shared<mockCollectionScheme>();
    std::map<std::string, ICollectionSchemePtr> mapEmpty;
    std::map<std::string, ICollectionSchemePtr> mapIdle = {
        { strCollectionSchemeIDCollectionScheme1, collectionScheme1 },
        { strCollectionSchemeIDCollectionScheme2, collectionScheme2 } };

    // setup maps
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1, mapEmpty, mapIdle );
    EXPECT_CALL( *collectionScheme1, getStartTime() )
        .WillOnce( Return( currTime.systemTimeMs ) )
        .WillOnce( Return( currTime.systemTimeMs ) );
    EXPECT_CALL( *collectionScheme1, getExpiryTime() ).WillOnce( Return( currTime.systemTimeMs + 100 ) );

    EXPECT_CALL( *collectionScheme2, getStartTime() )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) );
    EXPECT_CALL( *collectionScheme2, getExpiryTime() ).WillOnce( Return( currTime.systemTimeMs + 200 ) );

    // create mTimeLine
    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> testTimeLine;
    TimeData dataPair = { currTime, "Checkin" };
    testTimeLine.push( dataPair );
    dataPair = { currTime, strCollectionSchemeIDCollectionScheme1 };
    testTimeLine.push( dataPair );
    dataPair = { currTime + 10, strCollectionSchemeIDCollectionScheme2 };
    testTimeLine.push( dataPair );
    dataPair = { currTime + 20, strCollectionSchemeIDCollectionScheme2 };
    testTimeLine.push( dataPair );

    // test code
    CANInterfaceIDTranslator canIDTranslator;
    gmocktest.init( 200, nullptr, canIDTranslator );
    gmocktest.setTimeLine( testTimeLine );
    gmocktest.sendCheckin();
    ASSERT_TRUE( gmocktest.checkTimeLine( currTime ) );
    ASSERT_TRUE( gmocktest.checkTimeLine( currTime + 20 ) );
    // branch out startTime > currTime
    ASSERT_FALSE( gmocktest.checkTimeLine( currTime + 400 ) );
}

/** @brief
 * This test validates the state transition from non active to active of a Collection Scheme.
 */
TEST( CollectionSchemeManagerGtest, checkTimeLineTest_ENABLED_BRANCHES )
{
    // prepare input
    std::string strDecoderManifestID1 = "DM1";
    std::string strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    std::string strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";

    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();

    // create empty collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionScheme> collectionScheme2 = std::make_shared<mockCollectionScheme>();
    std::map<std::string, ICollectionSchemePtr> mapEmpty;
    std::map<std::string, ICollectionSchemePtr> mapEnable = {
        { strCollectionSchemeIDCollectionScheme1, collectionScheme1 },
        { strCollectionSchemeIDCollectionScheme2, collectionScheme2 } };

    // setup maps
    NiceMock<mockCollectionSchemeManagerTest> gmocktest( strDecoderManifestID1, mapEnable, mapEmpty );
    EXPECT_CALL( *collectionScheme1, getExpiryTime() )
        .WillOnce( Return( currTime.systemTimeMs ) )
        .WillOnce( Return( currTime.systemTimeMs ) );
    EXPECT_CALL( *collectionScheme1, getStartTime() ).WillOnce( Return( currTime.systemTimeMs - 100 ) );

    EXPECT_CALL( *collectionScheme2, getExpiryTime() )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) )
        .WillOnce( Return( currTime.systemTimeMs + 20 ) );
    EXPECT_CALL( *collectionScheme2, getStartTime() ).WillOnce( Return( currTime.systemTimeMs - 200 ) );

    // create mTimeLine
    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> testTimeLine;
    TimeData dataPair = { currTime, "Checkin" };
    testTimeLine.push( dataPair );
    dataPair = { currTime, strCollectionSchemeIDCollectionScheme1 };
    testTimeLine.push( dataPair );
    dataPair = { currTime + 10, strCollectionSchemeIDCollectionScheme2 };
    testTimeLine.push( dataPair );
    dataPair = { currTime + 20, strCollectionSchemeIDCollectionScheme2 };
    testTimeLine.push( dataPair );

    // test code
    CANInterfaceIDTranslator canIDTranslator;
    gmocktest.init( 200, nullptr, canIDTranslator );
    gmocktest.setTimeLine( testTimeLine );
    gmocktest.sendCheckin();
    ASSERT_TRUE( gmocktest.checkTimeLine( currTime ) );
    ASSERT_TRUE( gmocktest.checkTimeLine( currTime + 20 ) );
    // branch out startTime > currTime
    ASSERT_FALSE( gmocktest.checkTimeLine( currTime + 400 ) );
}

} // namespace IoTFleetWise
} // namespace Aws
