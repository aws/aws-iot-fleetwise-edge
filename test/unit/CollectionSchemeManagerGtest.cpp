// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManagerMock.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include "Testing.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionSchemeManager.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <queue>

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
    SyncID strDecoderManifestID1 = "DM1";
    SyncID strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    SyncID strDecoderManifestIDCollectionScheme1 = "DM1";
    SyncID strDecoderManifestIDCollectionScheme2 = "DM2";
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
    std::vector<std::shared_ptr<ICollectionScheme>> testCP, emptyCP;
    testCP.emplace_back( collectionScheme1 );
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), strDecoderManifestID1 );

    EXPECT_CALL( *testPL, getCollectionSchemes() )
        .WillOnce( ReturnRef( emptyCP ) )
        .WillOnce( ReturnRef( testCP ) )
        .WillOnce( ReturnRef( testCP ) )
        .WillOnce( ReturnRef( testCP ) );

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
    // 2. start time > currTime, add idle collectionScheme
    std::cout << COUT_GTEST_MGT << " 3. start time > currTime, add idle collectionScheme" << ANSI_TXT_DFT << std::endl;
    ASSERT_FALSE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    // 3. start time <= currTime && stop time > currTime add enable collectionScheme
    std::cout << COUT_GTEST_MGT << "4. start time <= currTime && stop time > currTime add enable collectionScheme"
              << ANSI_TXT_DFT << std::endl;
    ASSERT_TRUE( gmocktest.rebuildMapsandTimeLine( currTime ) );
    // 4. stop time < currTime already expired
    std::cout << COUT_GTEST_MGT << "5. stop time < currTime CollectionScheme already expired" << ANSI_TXT_DFT
              << std::endl;
    ASSERT_FALSE( gmocktest.rebuildMapsandTimeLine( currTime ) );
}

/** @brief
 * This test validates the life cycle of the Collection Schemes as they get
 * added/deleted by the Collection Scheme manager.
 */
TEST( CollectionSchemeManagerGtest, updateMapsandTimeLineTest_ADD_DELETE )
{
    // prepare input
    SyncID strDecoderManifestID1 = "DM1";
    SyncID strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    SyncID strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";
    SyncID strCollectionSchemeIDCollectionScheme3 = "COLLECTIONSCHEME3";

    SyncID strDecoderManifestIDCollectionScheme1 = "DM1";
    SyncID strDecoderManifestIDCollectionScheme2 = "DM2";
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
    std::vector<std::shared_ptr<ICollectionScheme>> testCP, emptyCP;
    // 3 collectionSchemes on the list, one enabled and one idle, one bad
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );
    testCP.emplace_back( collectionScheme1 );

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), strDecoderManifestID1 );

    EXPECT_CALL( *testPL, getCollectionSchemes() )
        .WillOnce( ReturnRef( emptyCP ) )
        // addition
        .WillOnce( ReturnRef( testCP ) )
        // deletion
        .WillOnce( ReturnRef( emptyCP ) );

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
    // 2. add all collectionSchemes and drop the collectionScheme with bad setting
    std::cout << COUT_GTEST_MGT << " 3. add all collectionSchemes and drop the collectionScheme with bad setting"
              << ANSI_TXT_DFT << std::endl;
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
    // 3. deletion
    std::cout << COUT_GTEST_MGT << "4. Delete all" << ANSI_TXT_DFT << std::endl;
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
}

/** @brief
 * This test validates the life cycle of the Collection Schemes as they get
 * to be set to idle/enabled by the Collection Scheme manager.
 */
TEST( CollectionSchemeManagerGtest, updateMapsandTimeLineTest_IDLE_BRANCHES )
{
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

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DM1" );
    auto idleCollectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startIdleTime, stopIdleTime );

    // test code
    gmocktest.setCollectionSchemeList( std::make_shared<ICollectionSchemeListTest>(
        std::vector<std::shared_ptr<ICollectionScheme>>{ idleCollectionScheme } ) );
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );

    // 1st update bad setting
    gmocktest.setCollectionSchemeList(
        std::make_shared<ICollectionSchemeListTest>( std::vector<std::shared_ptr<ICollectionScheme>>{
            std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", start2Time, stop2Time ) } ) );
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );

    // 2nd update new idle time
    gmocktest.setCollectionSchemeList(
        std::make_shared<ICollectionSchemeListTest>( std::vector<std::shared_ptr<ICollectionScheme>>{
            std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", start1Time, stop1Time ) } ) );
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );

    // 3rd update move to enable
    gmocktest.setCollectionSchemeList( std::make_shared<ICollectionSchemeListTest>(
        std::vector<std::shared_ptr<ICollectionScheme>>{ std::make_shared<ICollectionSchemeTest>(
            "COLLECTIONSCHEME1", "DM1", startEnableTime, stopEnableTime ) } ) );
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
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

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "COLLECTIONSCHEME1" );
    auto enabledCollectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startEnableTime, stopEnableTime );

    // test code
    gmocktest.setCollectionSchemeList( std::make_shared<ICollectionSchemeListTest>(
        std::vector<std::shared_ptr<ICollectionScheme>>{ enabledCollectionScheme } ) );
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );

    // 2nd update new idle time
    gmocktest.setCollectionSchemeList(
        std::make_shared<ICollectionSchemeListTest>( std::vector<std::shared_ptr<ICollectionScheme>>{
            std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", start1Time, stop1Time ) } ) );
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );

    // 1st update bad setting
    gmocktest.setCollectionSchemeList(
        std::make_shared<ICollectionSchemeListTest>( std::vector<std::shared_ptr<ICollectionScheme>>{
            std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", start2Time, stop2Time ) } ) );
    ASSERT_FALSE( gmocktest.updateMapsandTimeLine( currTime ) );

    // 3rd update move to enable
    gmocktest.setCollectionSchemeList(
        std::make_shared<ICollectionSchemeListTest>( std::vector<std::shared_ptr<ICollectionScheme>>{
            std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startExpTime, stopExpTime ) } ) );
    ASSERT_TRUE( gmocktest.updateMapsandTimeLine( currTime ) );
}

/** @brief
 * This test validates the checkin interval when no active collection schemes are in the system
 */
TEST( CollectionSchemeManagerGtest, checkTimeLineTest_IDLE_BRANCHES )
{
    // prepare input
    SyncID strDecoderManifestID1 = "DM1";
    SyncID strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    SyncID strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";

    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();

    // create empty collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionScheme> collectionScheme2 = std::make_shared<mockCollectionScheme>();
    std::map<SyncID, std::shared_ptr<ICollectionScheme>> mapEmpty;
    std::map<SyncID, std::shared_ptr<ICollectionScheme>> mapIdle = {
        { strCollectionSchemeIDCollectionScheme1, collectionScheme1 },
        { strCollectionSchemeIDCollectionScheme2, collectionScheme2 } };

    // setup maps
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest( nullptr,
                                              canIDTranslator,
                                              std::make_shared<CheckinSender>( nullptr ),
                                              strDecoderManifestID1,
                                              mapEmpty,
                                              mapIdle );
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
    gmocktest.setTimeLine( testTimeLine );
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
    SyncID strDecoderManifestID1 = "DM1";
    SyncID strCollectionSchemeIDCollectionScheme1 = "COLLECTIONSCHEME1";
    SyncID strCollectionSchemeIDCollectionScheme2 = "COLLECTIONSCHEME2";

    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();

    // create empty collectionScheme map
    std::shared_ptr<mockCollectionScheme> collectionScheme1 = std::make_shared<mockCollectionScheme>();
    std::shared_ptr<mockCollectionScheme> collectionScheme2 = std::make_shared<mockCollectionScheme>();
    std::map<SyncID, std::shared_ptr<ICollectionScheme>> mapEmpty;
    std::map<SyncID, std::shared_ptr<ICollectionScheme>> mapEnable = {
        { strCollectionSchemeIDCollectionScheme1, collectionScheme1 },
        { strCollectionSchemeIDCollectionScheme2, collectionScheme2 } };

    // setup maps
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest( nullptr,
                                              canIDTranslator,
                                              std::make_shared<CheckinSender>( nullptr ),
                                              strDecoderManifestID1,
                                              mapEnable,
                                              mapEmpty );
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
    gmocktest.setTimeLine( testTimeLine );
    ASSERT_TRUE( gmocktest.checkTimeLine( currTime ) );
    ASSERT_TRUE( gmocktest.checkTimeLine( currTime + 20 ) );
    // branch out startTime > currTime
    ASSERT_FALSE( gmocktest.checkTimeLine( currTime + 400 ) );
}

} // namespace IoTFleetWise
} // namespace Aws
