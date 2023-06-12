// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManagerTest.h"
#include "WaitUntil.h"

using namespace Aws::IoTFleetWise::TestingSupport;

/**********************test body ***********************************************/
TEST( CollectionSchemeManagerTest, StopMainTest )
{
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerTest test;
    test.init( 50, nullptr, canIDTranslator );
    test.myRegisterListener();
    ASSERT_TRUE( test.connect() );

    /* stopping idling main thread */
    WAIT_ASSERT_TRUE( test.disconnect() );

    /* build DMs */
    ASSERT_TRUE( test.connect() );
    IDecoderManifestPtr testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    std::vector<ICollectionSchemePtr> testList1;

    /* build collectionScheme list1 */
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    /* mock currTime, and 3 collectionSchemes */
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + SECOND_TO_MILLISECOND( 1 );
    Timestamp stopTime = startTime + SECOND_TO_MILLISECOND( 25 );
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    /* create ICollectionSchemeList */
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    /* sending lists and dm to PM */
    test.mDmTest = testDM1;
    test.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    test.myInvokeCollectionScheme();

    /* stopping main thread servicing a collectionScheme ending in 25 seconds */
    WAIT_ASSERT_TRUE( test.disconnect() );
}

TEST( CollectionSchemeManagerTest, CollectionSchemeUpdateCallBackTest )
{
    CollectionSchemeManagerTest test;
    std::vector<ICollectionSchemePtr> emptyList;
    CANInterfaceIDTranslator canIDTranslator;
    test.init( 50, nullptr, canIDTranslator );
    test.myRegisterListener();
    test.setmCollectionSchemeAvailable( false );
    test.setmProcessCollectionScheme( false );
    // pl is null
    test.mPlTest = nullptr;
    test.myInvokeCollectionScheme();
    ASSERT_TRUE( test.getmCollectionSchemeAvailable() );
    test.updateAvailable();
    ASSERT_FALSE( test.getmCollectionSchemeAvailable() );
    ASSERT_FALSE( test.getmProcessCollectionScheme() );
    // pl is valid
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( emptyList );
    test.myInvokeCollectionScheme();
    ASSERT_TRUE( test.getmCollectionSchemeAvailable() );
    test.updateAvailable();
    ASSERT_FALSE( test.getmCollectionSchemeAvailable() );
    ASSERT_TRUE( test.getmProcessCollectionScheme() );
}

TEST( CollectionSchemeManagerTest, DecoderManifestUpdateCallBackTest )
{
    CollectionSchemeManagerTest test;
    CANInterfaceIDTranslator canIDTranslator;
    test.init( 50, nullptr, canIDTranslator );
    test.myRegisterListener();
    test.setmDecoderManifestAvailable( false );
    test.setmProcessDecoderManifest( false );
    // dm is null
    test.mDmTest = nullptr;
    test.myInvokeDecoderManifest();
    ASSERT_TRUE( test.getmDecoderManifestAvailable() );
    test.updateAvailable();
    ASSERT_FALSE( test.getmDecoderManifestAvailable() );
    ASSERT_FALSE( test.getmProcessDecoderManifest() );
    // pl is valid
    test.mDmTest = std::make_shared<IDecoderManifestTest>( "" );
    test.myInvokeDecoderManifest();
    ASSERT_TRUE( test.getmDecoderManifestAvailable() );
    test.updateAvailable();
    ASSERT_FALSE( test.getmDecoderManifestAvailable() );
    ASSERT_TRUE( test.getmProcessDecoderManifest() );
}

TEST( CollectionSchemeManagerTest, MockProducerTest )
{
    /*
     * This is the integration test of PM. A mock producer is creating mocking CollectionScheme Ingestion sending
     * updates to PM. The following are the test police lists, and decoderManifest used. There will be 2 different
     * decoderManifest: DM1 and DM2. There will be 2 collectionScheme lists: list1 uses DM1, and list2 uses DM2.
     *
     * create list1 with 2 polices:
     * collectionScheme1(DM1, now+1sec, now+6sec);
     * collectionScheme2(DM1, now+3sec, now+8sec).
     *
     *
     * step1: adding and removing collectionScheme from PM
     * mockProducer sends list1 and DM1 to PM;
     * mockProducer waits for 1sec, and removes collectionScheme1 from list1, sends (CollectionScheme2) to PM indicating
     * to remove collectionScheme1; create additional collectionScheme3: (DM1, now+1sec, now+6sec) mockProducer sends
     * (collectionScheme2, collectionScheme3) to PM indicating adding collectionScheme3;
     *
     * step2: update DecoderManifest
     * create list3 with 2 polices:
     * collectionScheme1(DM2, now+1sec, now+6sec);
     * collectionScheme2(DM2, now+2sec, now+7sec).
     * mockProducer sends list2 and DM1 to PM; This indicates a coming change of decoderManifest. PM stops all
     * collectionSchemes; mockProducer waits for 1 sec, and sends list3 and DM2. PM starts rebuilding collectionScheme
     * maps and timeline.
     *
     * step3:
     * watching mTimeLine run to complete all collectionSchemes.
     */
    /* start PM main thread */
    CollectionSchemeManagerTest test;
    CANInterfaceIDTranslator canIDTranslator;
    test.init( 50, nullptr, canIDTranslator );
    test.myRegisterListener();
    ASSERT_TRUE( test.connect() );

    /* build DMs */
    IDecoderManifestPtr testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    IDecoderManifestPtr testDM2 = std::make_shared<IDecoderManifestTest>( "DM2" );
    std::vector<ICollectionSchemePtr> testList1, testList2, testList3;

    /* build collectionScheme list1 */
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + 100;
    Timestamp stopTime = startTime + 500;
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );

    startTime = currTime.systemTimeMs + 300;
    stopTime = startTime + 500;
    collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME2", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );
    /* create ICollectionSchemeList */
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    //////////////////// test starts here////////////////////////////////
    /* sending lists and dm to PM */
    std::cout << COUT_GTEST_MGT << "Step1: send DM1 and COLLECTIONSCHEME1 and COLLECTIONSCHEME2 " << ANSI_TXT_DFT
              << std::endl;
    test.mDmTest = testDM1;
    test.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    test.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // remove CollectionScheme1, don't send DM
    std::cout << COUT_GTEST_MGT << "Step2: remove COLLECTIONSCHEME1 " << ANSI_TXT_DFT << std::endl;
    testList2.emplace_back( testList1[1] );
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList2 );
    test.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    /* add COLLECTIONSCHEME3  */
    std::cout << COUT_GTEST_MGT << "Step3: add COLLECTIONSCHEME3 " << ANSI_TXT_DFT << std::endl;
    currTime = testClock->timeSinceEpoch();
    startTime = currTime.systemTimeMs + 100;
    stopTime = startTime + 500;
    collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME3", "DM1", startTime, stopTime );
    testList2.emplace_back( collectionScheme );
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList2 );
    test.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    /* send DM2 and list2(of DM1) to PM, PM will stop all collectionSchemes */
    std::cout << COUT_GTEST_MGT << "Step4: send DM2 " << ANSI_TXT_DFT << std::endl;
    test.mDmTest = testDM2;
    test.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    std::cout << COUT_GTEST_MGT << "Step5: send DM1 again " << ANSI_TXT_DFT << std::endl;
    test.mDmTest = testDM1;
    test.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    std::cout << COUT_GTEST_MGT << "Step6: send DM2 again " << ANSI_TXT_DFT << std::endl;
    test.mDmTest = testDM2;
    test.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    /* build list3 with DM2 */
    currTime = testClock->timeSinceEpoch();
    startTime = currTime.systemTimeMs + 100;
    stopTime = startTime + 500;
    collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME4", "DM2", startTime, stopTime );
    testList3.emplace_back( collectionScheme );
    startTime = currTime.systemTimeMs + 200;
    stopTime = startTime + 500;
    collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME5", "DM2", startTime, stopTime );
    testList3.emplace_back( collectionScheme );
    /* send list3 to PM, PM will start rebuilding all collectionSchemes */
    std::cout << COUT_GTEST_MGT << "Step7: send COLLECTIONSCHEME4 and COLLECTIONSCHEME5 " << ANSI_TXT_DFT << std::endl;
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList3 );
    test.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    /* send empty list of any DM */
    std::cout << COUT_GTEST_MGT << "Step7: send empty collectionSchemeList " << ANSI_TXT_DFT << std::endl;
    testList2.clear();
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList2 );
    test.myInvokeCollectionScheme();

    WAIT_ASSERT_TRUE( test.disconnect() );
}

TEST( CollectionSchemeManagerTest, getCollectionSchemeArns )
{
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerTest test;
    test.init( 50, nullptr, canIDTranslator );
    test.myRegisterListener();
    ASSERT_TRUE( test.connect() );

    ASSERT_EQ( test.getCollectionSchemeArns(), std::vector<std::string>() );

    /* build DMs */
    IDecoderManifestPtr testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    std::vector<ICollectionSchemePtr> testList1;

    /* build collectionScheme list1 */
    std::shared_ptr<const Clock> testClock = ClockHandler::getClock();
    /* mock currTime, and 3 collectionSchemes */
    TimePoint currTime = testClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + SECOND_TO_MILLISECOND( 1 );
    Timestamp stopTime = startTime + SECOND_TO_MILLISECOND( 25 );
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    /* create ICollectionSchemeList */
    test.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    /* sending lists and dm to PM */
    test.mDmTest = testDM1;
    test.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    test.myInvokeCollectionScheme();

    WAIT_ASSERT_EQ( test.getCollectionSchemeArns(), std::vector<std::string>( { "COLLECTIONSCHEME1" } ) );

    /* stopping main thread servicing a collectionScheme ending in 25 seconds */
    WAIT_ASSERT_TRUE( test.disconnect() );
}
