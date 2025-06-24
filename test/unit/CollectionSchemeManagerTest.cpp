// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManagerTest.h"
#include "CollectionSchemeManagerMock.h"
#include "Testing.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/SchemaListener.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "collection_schemes.pb.h"
#include "decoder_manifest.pb.h"
#include <chrono>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>
#include <mutex>
#include <thread>

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "aws/iotfleetwise/LastKnownStateTypes.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::InvokeArgument;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

class SchemaListenerMock : public SchemaListener
{
public:
    void
    sendCheckin( const std::vector<SyncID> &documentARNs, OnCheckinSentCallback callback ) override
    {
        mockedSendCheckin( documentARNs, callback );
        std::lock_guard<std::mutex> lock( sentDocumentsMutex );
        mSentDocuments.push_back( documentARNs );
    }

    MOCK_METHOD( void, mockedSendCheckin, ( const std::vector<SyncID> &documentARNs, OnCheckinSentCallback callback ) );

    std::vector<std::vector<SyncID>>
    getSentDocuments()
    {
        std::lock_guard<std::mutex> lock( sentDocumentsMutex );
        return mSentDocuments;
    }

    int
    getLastSentDocuments( std::vector<SyncID> &sentDocuments )
    {
        std::lock_guard<std::mutex> lock( sentDocumentsMutex );
        if ( mSentDocuments.empty() )
        {
            return -1;
        }
        sentDocuments = mSentDocuments.back();
        return static_cast<int>( sentDocuments.size() );
    }

private:
    std::vector<std::vector<SyncID>> mSentDocuments;
    std::mutex sentDocumentsMutex;
};

class CollectionSchemeManagerTest : public ::testing::Test
{
protected:
    CollectionSchemeManagerTest()
        : mCollectionSchemeManager( nullptr, mCanIDTranslator, std::make_shared<CheckinSender>( nullptr ) )
    {
    }

    void
    SetUp() override
    {
        mCollectionSchemeManager.subscribeToInspectionMatrixChange(
            [&]( std::shared_ptr<const InspectionMatrix> inspectionMatrix ) {
                mReceivedInspectionMatrices.emplace_back( inspectionMatrix );
            } );

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        mCollectionSchemeManager.subscribeToStateTemplatesChange(
            [&]( std::shared_ptr<StateTemplateList> stateTemplates ) {
                std::lock_guard<std::mutex> lock( mReceivedStateTemplatesMutex );
                mReceivedStateTemplates.emplace_back( *stateTemplates );
            } );
#endif
    }

    void
    TearDown() override
    {
        WAIT_ASSERT_TRUE( mCollectionSchemeManager.disconnect() );
    }

    CANInterfaceIDTranslator mCanIDTranslator;
    CollectionSchemeManagerWrapper mCollectionSchemeManager;
    std::vector<std::shared_ptr<const InspectionMatrix>> mReceivedInspectionMatrices;
    std::shared_ptr<const Clock> mTestClock = ClockHandler::getClock();

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    std::vector<StateTemplateList>
    getReceivedStateTemplates()
    {
        std::vector<StateTemplateList> sortedStateTemplates;
        std::lock_guard<std::mutex> lock( mReceivedStateTemplatesMutex );
        for ( auto stateTemplateList : mReceivedStateTemplates )
        {
            sort( stateTemplateList.begin(), stateTemplateList.end(), []( const auto &a, const auto &b ) {
                return a->id < b->id;
            } );
            sortedStateTemplates.emplace_back( stateTemplateList );
        }
        return sortedStateTemplates;
    };

    std::mutex mReceivedStateTemplatesMutex;
    std::vector<StateTemplateList> mReceivedStateTemplates;
#endif
};

std::vector<SignalID>
getSignalIdsFromCondition( const ConditionWithCollectedData &condition )
{
    std::vector<SignalID> signalIds;
    for ( const auto &signal : condition.signals )
    {
        signalIds.push_back( signal.signalID );
    }
    return signalIds;
}

TEST_F( CollectionSchemeManagerTest, StopMain )
{
    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    /* stopping idling main thread */
    WAIT_ASSERT_TRUE( mCollectionSchemeManager.disconnect() );

    /* build DMs */
    ASSERT_TRUE( mCollectionSchemeManager.connect() );
    auto testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    std::vector<std::shared_ptr<ICollectionScheme>> testList1;

    /* build collectionScheme list1 */
    /* mock currTime, and 3 collectionSchemes */
    TimePoint currTime = mTestClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + SECOND_TO_MILLISECOND( 1 );
    Timestamp stopTime = startTime + SECOND_TO_MILLISECOND( 25 );
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    /* create ICollectionSchemeList */
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    /* sending lists and dm to PM */
    mCollectionSchemeManager.mDmTest = testDM1;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    mCollectionSchemeManager.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    auto lastKnownStateIngestionMock = std::make_shared<LastKnownStateIngestionMock>();
    mCollectionSchemeManager.mLastKnownStateIngestionTest = lastKnownStateIngestionMock;
    // Build failed:
    EXPECT_CALL( *lastKnownStateIngestionMock, build() ).WillRepeatedly( Return( false ) );
    mCollectionSchemeManager.myInvokeStateTemplates();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    // Build successful, DM out of sync:
    EXPECT_CALL( *lastKnownStateIngestionMock, build() ).WillRepeatedly( Return( true ) );

    {
        auto stateTemplate = std::make_shared<StateTemplateInformation>();
        stateTemplate->id = "LKS1";
        stateTemplate->decoderManifestID = "DM2";
        auto stateTemplatesDiff = std::make_shared<StateTemplatesDiff>();
        stateTemplatesDiff->stateTemplatesToAdd.emplace_back( stateTemplate );
        EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
            .WillRepeatedly( Return( stateTemplatesDiff ) );
        mCollectionSchemeManager.myInvokeStateTemplates();
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // DM in sync:
    {
        auto stateTemplate = std::make_shared<StateTemplateInformation>();
        stateTemplate->id = "LKS2";
        stateTemplate->decoderManifestID = "DM1";
        auto stateTemplatesDiff = std::make_shared<StateTemplatesDiff>();
        stateTemplatesDiff->stateTemplatesToAdd.emplace_back( stateTemplate );
        EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
            .WillRepeatedly( Return( stateTemplatesDiff ) );
        mCollectionSchemeManager.myInvokeStateTemplates();
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    // Ignore with the same sync id:
    mCollectionSchemeManager.myInvokeStateTemplates();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
#endif

    /* stopping main thread servicing a collectionScheme ending in 25 seconds */
    WAIT_ASSERT_TRUE( mCollectionSchemeManager.disconnect() );
}

TEST_F( CollectionSchemeManagerTest, CollectionSchemeUpdateCallBack )
{
    std::vector<std::shared_ptr<ICollectionScheme>> emptyList;
    mCollectionSchemeManager.setmCollectionSchemeAvailable( false );
    mCollectionSchemeManager.setmProcessCollectionScheme( false );
    // pl is null
    mCollectionSchemeManager.mPlTest = nullptr;
    mCollectionSchemeManager.myInvokeCollectionScheme();
    ASSERT_TRUE( mCollectionSchemeManager.getmCollectionSchemeAvailable() );
    mCollectionSchemeManager.updateAvailable();
    ASSERT_FALSE( mCollectionSchemeManager.getmCollectionSchemeAvailable() );
    ASSERT_FALSE( mCollectionSchemeManager.getmProcessCollectionScheme() );
    // pl is valid
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( emptyList );
    mCollectionSchemeManager.myInvokeCollectionScheme();
    ASSERT_TRUE( mCollectionSchemeManager.getmCollectionSchemeAvailable() );
    mCollectionSchemeManager.updateAvailable();
    ASSERT_FALSE( mCollectionSchemeManager.getmCollectionSchemeAvailable() );
    ASSERT_TRUE( mCollectionSchemeManager.getmProcessCollectionScheme() );
}

TEST_F( CollectionSchemeManagerTest, DecoderManifestUpdateCallBack )
{
    mCollectionSchemeManager.setmDecoderManifestAvailable( false );
    mCollectionSchemeManager.setmProcessDecoderManifest( false );
    // dm is null
    mCollectionSchemeManager.mDmTest = nullptr;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    ASSERT_TRUE( mCollectionSchemeManager.getmDecoderManifestAvailable() );
    mCollectionSchemeManager.updateAvailable();
    ASSERT_FALSE( mCollectionSchemeManager.getmDecoderManifestAvailable() );
    ASSERT_FALSE( mCollectionSchemeManager.getmProcessDecoderManifest() );
    // pl is valid
    mCollectionSchemeManager.mDmTest = std::make_shared<IDecoderManifestTest>( "" );
    mCollectionSchemeManager.myInvokeDecoderManifest();
    ASSERT_TRUE( mCollectionSchemeManager.getmDecoderManifestAvailable() );
    mCollectionSchemeManager.updateAvailable();
    ASSERT_FALSE( mCollectionSchemeManager.getmDecoderManifestAvailable() );
    ASSERT_TRUE( mCollectionSchemeManager.getmProcessDecoderManifest() );
}

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
TEST_F( CollectionSchemeManagerTest, StateTemplatesUpdate )
{
    auto decoderManifestIngestionMock = std::make_shared<mockDecoderManifest>();
    EXPECT_CALL( *decoderManifestIngestionMock, build() ).WillRepeatedly( Return( true ) );
    EXPECT_CALL( *decoderManifestIngestionMock, getID() ).WillRepeatedly( Return( "decoder1" ) );
    auto lastKnownStateIngestionMock = std::make_shared<LastKnownStateIngestionMock>();
    EXPECT_CALL( *lastKnownStateIngestionMock, build() ).WillRepeatedly( Return( true ) );

    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    mCollectionSchemeManager.onDecoderManifestUpdate( decoderManifestIngestionMock );

    // This should be an empty list, triggered by the decoder manifest update
    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 1U );
    auto receivedStateTemplates = getReceivedStateTemplates();
    auto stateTemplates = receivedStateTemplates[0];
    ASSERT_EQ( stateTemplates.size(), 0U );

    auto stateTemplateToAdd = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate1",
                                  "decoder1",
                                  { LastKnownStateSignalInformation{ 1 }, LastKnownStateSignalInformation{ 2 } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  500 } );

    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, { stateTemplateToAdd }, {} } ) ) );

    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 2U );
    receivedStateTemplates = getReceivedStateTemplates();

    stateTemplates = receivedStateTemplates[1];
    ASSERT_EQ( stateTemplates.size(), 1U );
    ASSERT_EQ( stateTemplates[0]->id, "stateTemplate1" );
    ASSERT_EQ( stateTemplates[0]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[0]->signals.size(), 2U );
    ASSERT_EQ( stateTemplates[0]->signals[0].signalID, 1 );
    ASSERT_EQ( stateTemplates[0]->signals[1].signalID, 2 );
    ASSERT_EQ( stateTemplates[0]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[0]->periodMs, 500U );

    stateTemplateToAdd = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate2",
                                  "decoder1",
                                  { LastKnownStateSignalInformation{ 7 } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  400 } );
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, { stateTemplateToAdd }, {} } ) ) );

    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 3U );
    receivedStateTemplates = getReceivedStateTemplates();

    stateTemplates = receivedStateTemplates[2];
    ASSERT_EQ( stateTemplates.size(), 2U );

    ASSERT_EQ( stateTemplates[0]->id, "stateTemplate1" );
    ASSERT_EQ( stateTemplates[0]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[0]->signals.size(), 2U );
    ASSERT_EQ( stateTemplates[0]->signals[0].signalID, 1 );
    ASSERT_EQ( stateTemplates[0]->signals[1].signalID, 2 );
    ASSERT_EQ( stateTemplates[0]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[0]->periodMs, 500U );

    ASSERT_EQ( stateTemplates[1]->id, "stateTemplate2" );
    ASSERT_EQ( stateTemplates[1]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[1]->signals.size(), 1U );
    ASSERT_EQ( stateTemplates[1]->signals[0].signalID, 7 );
    ASSERT_EQ( stateTemplates[1]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[1]->periodMs, 400U );

    std::vector<SyncID> stateTemplatesToRemove{ "stateTemplate1" };
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, {}, stateTemplatesToRemove } ) ) );

    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 4U );
    receivedStateTemplates = getReceivedStateTemplates();

    stateTemplates = receivedStateTemplates[3];
    ASSERT_EQ( stateTemplates.size(), 1U );

    ASSERT_EQ( stateTemplates[0]->id, "stateTemplate2" );
    ASSERT_EQ( stateTemplates[0]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[0]->signals.size(), 1U );
    ASSERT_EQ( stateTemplates[0]->signals[0].signalID, 7 );
    ASSERT_EQ( stateTemplates[0]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[0]->periodMs, 400U );
}

TEST_F( CollectionSchemeManagerTest, StateTemplatesUpdateRejectDiffWithLowerVersion )
{
    auto decoderManifestIngestionMock = std::make_shared<mockDecoderManifest>();
    EXPECT_CALL( *decoderManifestIngestionMock, build() ).WillRepeatedly( Return( true ) );
    EXPECT_CALL( *decoderManifestIngestionMock, getID() ).WillRepeatedly( Return( "decoder1" ) );
    auto lastKnownStateIngestionMock = std::make_shared<LastKnownStateIngestionMock>();
    EXPECT_CALL( *lastKnownStateIngestionMock, build() ).WillRepeatedly( Return( true ) );

    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    mCollectionSchemeManager.onDecoderManifestUpdate( decoderManifestIngestionMock );

    // This should be an empty list, triggered by the decoder manifest update
    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 1U );

    auto stateTemplateToAdd = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate1",
                                  "decoder1",
                                  { LastKnownStateSignalInformation{ 1 }, LastKnownStateSignalInformation{ 2 } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  500 } );

    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, { stateTemplateToAdd }, {} } ) ) );

    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 2U );
    auto receivedStateTemplates = getReceivedStateTemplates();

    auto stateTemplates = receivedStateTemplates[1];
    ASSERT_EQ( stateTemplates.size(), 1U );
    ASSERT_EQ( stateTemplates[0]->id, "stateTemplate1" );

    stateTemplateToAdd = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate2",
                                  "decoder1",
                                  { LastKnownStateSignalInformation{ 7 } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  400 } );
    // This has a smaller version than the previous received message, it should be ignored.
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 455, { stateTemplateToAdd }, {} } ) ) );
    auto previousSize = receivedStateTemplates.size();
    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );
    DELAY_ASSERT_TRUE( getReceivedStateTemplates().size() == previousSize );

    // This has the same version of the last accepted message, it should be processed normally.
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, { stateTemplateToAdd }, {} } ) ) );

    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 3U );
    receivedStateTemplates = getReceivedStateTemplates();

    stateTemplates = receivedStateTemplates[2];
    ASSERT_EQ( stateTemplates.size(), 2U );

    ASSERT_EQ( stateTemplates[0]->id, "stateTemplate1" );
    ASSERT_EQ( stateTemplates[0]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[0]->signals.size(), 2U );
    ASSERT_EQ( stateTemplates[0]->signals[0].signalID, 1 );
    ASSERT_EQ( stateTemplates[0]->signals[1].signalID, 2 );
    ASSERT_EQ( stateTemplates[0]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[0]->periodMs, 500U );

    ASSERT_EQ( stateTemplates[1]->id, "stateTemplate2" );
    ASSERT_EQ( stateTemplates[1]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[1]->signals.size(), 1U );
    ASSERT_EQ( stateTemplates[1]->signals[0].signalID, 7 );
    ASSERT_EQ( stateTemplates[1]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[1]->periodMs, 400U );

    std::vector<SyncID> stateTemplatesToRemove{ "stateTemplate1" };
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, {}, stateTemplatesToRemove } ) ) );

    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 4U );
    receivedStateTemplates = getReceivedStateTemplates();

    stateTemplates = receivedStateTemplates[3];
    ASSERT_EQ( stateTemplates.size(), 1U );

    ASSERT_EQ( stateTemplates[0]->id, "stateTemplate2" );
    ASSERT_EQ( stateTemplates[0]->decoderManifestID, "decoder1" );
    ASSERT_EQ( stateTemplates[0]->signals.size(), 1U );
    ASSERT_EQ( stateTemplates[0]->signals[0].signalID, 7 );
    ASSERT_EQ( stateTemplates[0]->updateStrategy, LastKnownStateUpdateStrategy::PERIODIC );
    ASSERT_EQ( stateTemplates[0]->periodMs, 400U );
}

TEST_F( CollectionSchemeManagerTest, StateTemplatesUpdateRemoveNonExistingTemplate )
{
    auto lastKnownStateIngestionMock = std::make_shared<LastKnownStateIngestionMock>();

    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    auto decoderManifestIngestionMock = std::make_shared<mockDecoderManifest>();
    EXPECT_CALL( *decoderManifestIngestionMock, build() ).WillRepeatedly( Return( true ) );
    EXPECT_CALL( *decoderManifestIngestionMock, getID() ).WillRepeatedly( Return( "decoder1" ) );
    mCollectionSchemeManager.onDecoderManifestUpdate( decoderManifestIngestionMock );

    // This should be an empty list, triggered by the decoder manifest update
    WAIT_ASSERT_EQ( getReceivedStateTemplates().size(), 1U );

    std::vector<SyncID> stateTemplatesToRemove{ "stateTemplate1" };
    EXPECT_CALL( *lastKnownStateIngestionMock, build() ).WillRepeatedly( Return( true ) );
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillOnce(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 456, {}, stateTemplatesToRemove } ) ) );

    auto previousSize = getReceivedStateTemplates().size();
    mCollectionSchemeManager.onStateTemplatesChanged( lastKnownStateIngestionMock );

    DELAY_ASSERT_TRUE( getReceivedStateTemplates().size() == previousSize );
}
#endif

TEST_F( CollectionSchemeManagerTest, MockProducer )
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
    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    /* build DMs */
    auto testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    auto testDM2 = std::make_shared<IDecoderManifestTest>( "DM2" );
    std::vector<std::shared_ptr<ICollectionScheme>> testList1;
    std::vector<std::shared_ptr<ICollectionScheme>> testList2;
    std::vector<std::shared_ptr<ICollectionScheme>> testList3;

    /* build collectionScheme list1 */
    TimePoint currTime = mTestClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + 100;
    Timestamp stopTime = startTime + 500;
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );

    startTime = currTime.systemTimeMs + 300;
    stopTime = startTime + 500;
    collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME2", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );
    /* create ICollectionSchemeList */
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    //////////////////// test starts here////////////////////////////////
    /* sending lists and dm to PM */
    std::cout << COUT_GTEST_MGT << "Step1: send DM1 and COLLECTIONSCHEME1 and COLLECTIONSCHEME2 " << ANSI_TXT_DFT
              << std::endl;
    mCollectionSchemeManager.mDmTest = testDM1;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    mCollectionSchemeManager.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // remove CollectionScheme1, don't send DM
    std::cout << COUT_GTEST_MGT << "Step2: remove COLLECTIONSCHEME1 " << ANSI_TXT_DFT << std::endl;
    testList2.emplace_back( testList1[1] );
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList2 );
    mCollectionSchemeManager.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    /* add COLLECTIONSCHEME3  */
    std::cout << COUT_GTEST_MGT << "Step3: add COLLECTIONSCHEME3 " << ANSI_TXT_DFT << std::endl;
    currTime = mTestClock->timeSinceEpoch();
    startTime = currTime.systemTimeMs + 100;
    stopTime = startTime + 500;
    collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME3", "DM1", startTime, stopTime );
    testList2.emplace_back( collectionScheme );
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList2 );
    mCollectionSchemeManager.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    /* send DM2 and list2(of DM1) to PM, PM will stop all collectionSchemes */
    std::cout << COUT_GTEST_MGT << "Step4: send DM2 " << ANSI_TXT_DFT << std::endl;
    mCollectionSchemeManager.mDmTest = testDM2;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    std::cout << COUT_GTEST_MGT << "Step5: send DM1 again " << ANSI_TXT_DFT << std::endl;
    mCollectionSchemeManager.mDmTest = testDM1;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    std::cout << COUT_GTEST_MGT << "Step6: send DM2 again " << ANSI_TXT_DFT << std::endl;
    mCollectionSchemeManager.mDmTest = testDM2;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    /* build list3 with DM2 */
    currTime = mTestClock->timeSinceEpoch();
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
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList3 );
    mCollectionSchemeManager.myInvokeCollectionScheme();
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    /* send empty list of any DM */
    std::cout << COUT_GTEST_MGT << "Step7: send empty collectionSchemeList " << ANSI_TXT_DFT << std::endl;
    testList2.clear();
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList2 );
    mCollectionSchemeManager.myInvokeCollectionScheme();

    WAIT_ASSERT_TRUE( mCollectionSchemeManager.disconnect() );
}

TEST_F( CollectionSchemeManagerTest, getCollectionSchemeArns )
{
    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    ASSERT_EQ( mCollectionSchemeManager.getCollectionSchemeArns(), std::vector<SyncID>() );

    /* build DMs */
    auto testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    std::vector<std::shared_ptr<ICollectionScheme>> testList1;

    /* build collectionScheme list1 */
    /* mock currTime, and 3 collectionSchemes */
    TimePoint currTime = mTestClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + SECOND_TO_MILLISECOND( 1 );
    Timestamp stopTime = startTime + SECOND_TO_MILLISECOND( 25 );
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime );
    testList1.emplace_back( collectionScheme );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    /* create ICollectionSchemeList */
    mCollectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    /* sending lists and dm to PM */
    mCollectionSchemeManager.mDmTest = testDM1;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    mCollectionSchemeManager.myInvokeCollectionScheme();

    WAIT_ASSERT_EQ( mCollectionSchemeManager.getCollectionSchemeArns(),
                    std::vector<SyncID>( { "COLLECTIONSCHEME1" } ) );

    /* stopping main thread servicing a collectionScheme ending in 25 seconds */
    WAIT_ASSERT_TRUE( mCollectionSchemeManager.disconnect() );
}

TEST_F( CollectionSchemeManagerTest, SendCheckinPeriodically )
{
    uint32_t checkinIntervalMs = 100;
    auto schemaListenerMock = std::make_shared<StrictMock<SchemaListenerMock>>();
    auto checkinSender = std::make_shared<CheckinSender>( schemaListenerMock, checkinIntervalMs );
    CollectionSchemeManagerWrapper collectionSchemeManager( nullptr, mCanIDTranslator, checkinSender );

    EXPECT_CALL( *schemaListenerMock, mockedSendCheckin( _, _ ) ).WillRepeatedly( InvokeArgument<1>( true ) );

    std::vector<SyncID> sentDocuments;
    ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), -1 );
    ASSERT_TRUE( checkinSender->start() );

    // No checkin should be sent until CollectionSchemeManager is started, because if CollectionSchemeManager
    // restores data from persistency layer, the first checkin message should contain the restored
    // documents instead of being an empty checkin.
    std::this_thread::sleep_for( std::chrono::milliseconds( checkinIntervalMs * 2 ) );
    ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), -1 );

    ASSERT_TRUE( collectionSchemeManager.connect() );

    // Now that CollectionSchemeManager is started and nothing is persisted, the first checkin should
    // be empty
    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 0 );

    /* build DMs */
    auto testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    std::vector<std::shared_ptr<ICollectionScheme>> testList1;
    /* build collectionScheme list1 */
    TimePoint currTime = mTestClock->timeSinceEpoch();
    Timestamp startTime = currTime.systemTimeMs + SECOND_TO_MILLISECOND( 1 );
    Timestamp stopTime = startTime + SECOND_TO_MILLISECOND( 25 );
    testList1.emplace_back(
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", startTime, stopTime ) );
    testList1.emplace_back(
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME2", "DM1", startTime, stopTime ) );

    collectionSchemeManager.mPlTest = std::make_shared<ICollectionSchemeListTest>( testList1 );
    collectionSchemeManager.mDmTest = testDM1;
    collectionSchemeManager.myInvokeDecoderManifest();
    collectionSchemeManager.myInvokeCollectionScheme();

    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 3 );
    ASSERT_EQ( sentDocuments[0], "COLLECTIONSCHEME1" );
    ASSERT_EQ( sentDocuments[1], "COLLECTIONSCHEME2" );
    ASSERT_EQ( sentDocuments[2], "DM1" );

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    auto lastKnownStateIngestionMock = std::make_shared<LastKnownStateIngestionMock>();
    collectionSchemeManager.mLastKnownStateIngestionTest = lastKnownStateIngestionMock;
    auto stateTemplate1 = std::make_shared<StateTemplateInformation>();
    stateTemplate1->id = "LKS1";
    stateTemplate1->decoderManifestID = "DM1";
    EXPECT_CALL( *lastKnownStateIngestionMock, build() ).WillRepeatedly( Return( true ) );
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillRepeatedly(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 123, { stateTemplate1 }, {} } ) ) );
    collectionSchemeManager.myInvokeStateTemplates();

    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 4 );
    sort( sentDocuments.begin(), sentDocuments.end() );
    ASSERT_EQ( sentDocuments[0], "COLLECTIONSCHEME1" );
    ASSERT_EQ( sentDocuments[1], "COLLECTIONSCHEME2" );
    ASSERT_EQ( sentDocuments[2], "DM1" );
    ASSERT_EQ( sentDocuments[3], "LKS1" );

    // Add another template
    auto stateTemplate2 = std::make_shared<StateTemplateInformation>();
    stateTemplate2->id = "LKS2";
    stateTemplate2->decoderManifestID = "DM1";
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillRepeatedly(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 123, { stateTemplate2 }, {} } ) ) );
    collectionSchemeManager.myInvokeStateTemplates();

    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 5 );
    sort( sentDocuments.begin(), sentDocuments.end() );
    ASSERT_EQ( sentDocuments[0], "COLLECTIONSCHEME1" );
    ASSERT_EQ( sentDocuments[1], "COLLECTIONSCHEME2" );
    ASSERT_EQ( sentDocuments[2], "DM1" );
    ASSERT_EQ( sentDocuments[3], "LKS1" );
    ASSERT_EQ( sentDocuments[4], "LKS2" );

    // Remove an existing template
    EXPECT_CALL( *lastKnownStateIngestionMock, getStateTemplatesDiff() )
        .WillRepeatedly(
            Return( std::make_shared<StateTemplatesDiff>( StateTemplatesDiff{ 123, {}, { stateTemplate1->id } } ) ) );
    collectionSchemeManager.myInvokeStateTemplates();

    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 4 );
    sort( sentDocuments.begin(), sentDocuments.end() );
    ASSERT_EQ( sentDocuments[0], "COLLECTIONSCHEME1" );
    ASSERT_EQ( sentDocuments[1], "COLLECTIONSCHEME2" );
    ASSERT_EQ( sentDocuments[2], "DM1" );
    ASSERT_EQ( sentDocuments[3], "LKS2" );
#endif

    auto numOfMessagesSent = schemaListenerMock->getSentDocuments().size();

    std::this_thread::sleep_for( std::chrono::milliseconds( checkinIntervalMs * 5 ) );

    // Make sure checkin is sent periodically, but leave some margin for small timing differences
    ASSERT_GE( schemaListenerMock->getSentDocuments().size(), numOfMessagesSent + 4U );
    ASSERT_LT( schemaListenerMock->getSentDocuments().size(), numOfMessagesSent + 6U );
}

TEST_F( CollectionSchemeManagerTest, SendFirstCheckinWithPersistedDocuments )
{
    auto storage = createCacheAndPersist();

    // Store some documents
    {

        Schemas::DecoderManifestMsg::DecoderManifest protoDM;
        protoDM.set_sync_id( "DM1" );
        Schemas::DecoderManifestMsg::CANSignal *protoCANSignalA = protoDM.add_can_signals();
        protoCANSignalA->set_signal_id( 3908 );

        std::string protoSerializedBuffer;
        ASSERT_TRUE( protoDM.SerializeToString( &protoSerializedBuffer ) );
        ASSERT_EQ( storage->write( reinterpret_cast<const uint8_t *>( protoSerializedBuffer.data() ),
                                   protoSerializedBuffer.size(),
                                   DataType::DECODER_MANIFEST ),
                   ErrorCode::SUCCESS );
    }

    {
        Schemas::CollectionSchemesMsg::CollectionSchemes protoCollectionSchemesMsg;
        auto collectionScheme1 = protoCollectionSchemesMsg.add_collection_schemes();
        collectionScheme1->set_campaign_sync_id( "COLLECTIONSCHEME1" );
        collectionScheme1->set_decoder_manifest_sync_id( "DM1" );
        Schemas::CollectionSchemesMsg::TimeBasedCollectionScheme *timeBasedCollectionScheme =
            collectionScheme1->mutable_time_based_collection_scheme();
        timeBasedCollectionScheme->set_time_based_collection_scheme_period_ms( 5000 );
        collectionScheme1->set_expiry_time_ms_epoch( mTestClock->systemTimeSinceEpochMs() + 30000 );
        auto collectionScheme2 = protoCollectionSchemesMsg.add_collection_schemes();
        collectionScheme2->set_campaign_sync_id( "COLLECTIONSCHEME2" );
        collectionScheme2->set_decoder_manifest_sync_id( "DM1" );
        collectionScheme2->set_expiry_time_ms_epoch( mTestClock->systemTimeSinceEpochMs() + 30000 );
        timeBasedCollectionScheme = collectionScheme2->mutable_time_based_collection_scheme();
        timeBasedCollectionScheme->set_time_based_collection_scheme_period_ms( 5000 );

        std::string protoSerializedBuffer;
        ASSERT_TRUE( protoCollectionSchemesMsg.SerializeToString( &protoSerializedBuffer ) );
        ASSERT_EQ( storage->write( reinterpret_cast<const uint8_t *>( protoSerializedBuffer.data() ),
                                   protoSerializedBuffer.size(),
                                   DataType::COLLECTION_SCHEME_LIST ),
                   ErrorCode::SUCCESS );
    }

    uint32_t checkinIntervalMs = 100;
    auto schemaListenerMock = std::make_shared<StrictMock<SchemaListenerMock>>();
    auto checkinSender = std::make_shared<CheckinSender>( schemaListenerMock, checkinIntervalMs );
    CollectionSchemeManagerWrapper collectionSchemeManager( storage, mCanIDTranslator, checkinSender );

    EXPECT_CALL( *schemaListenerMock, mockedSendCheckin( _, _ ) ).WillRepeatedly( InvokeArgument<1>( true ) );

    std::vector<SyncID> sentDocuments;
    ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), -1 );
    ASSERT_TRUE( checkinSender->start() );

    // No checkin should be sent until CollectionSchemeManager is started, because if CollectionSchemeManager
    // restores data from persistency layer, the first checkin message should contain the restored
    // documents instead of being an empty checkin.
    std::this_thread::sleep_for( std::chrono::milliseconds( checkinIntervalMs * 2 ) );
    ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), -1 );

    ASSERT_TRUE( collectionSchemeManager.connect() );

    // Now that CollectionSchemeManager is started and restored persisted data, the first checkin should
    // contain the restored documents.
    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 3 );
    ASSERT_EQ( sentDocuments[0], "COLLECTIONSCHEME1" );
    ASSERT_EQ( sentDocuments[1], "COLLECTIONSCHEME2" );
    ASSERT_EQ( sentDocuments[2], "DM1" );
}

TEST_F( CollectionSchemeManagerTest, RetryCheckinOnFailure )
{
    uint32_t checkinIntervalMs = 100;
    auto schemaListenerMock = std::make_shared<StrictMock<SchemaListenerMock>>();
    auto checkinSender = std::make_shared<CheckinSender>( schemaListenerMock, checkinIntervalMs );
    CollectionSchemeManagerWrapper collectionSchemeManager( nullptr, mCanIDTranslator, checkinSender );

    EXPECT_CALL( *schemaListenerMock, mockedSendCheckin( _, _ ) ).WillRepeatedly( InvokeArgument<1>( true ) );

    std::vector<SyncID> sentDocuments;
    ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), -1 );
    ASSERT_TRUE( checkinSender->start() );

    // No checkin should be sent until CollectionSchemeManager is started, because if CollectionSchemeManager
    // restores data from persistency layer, the first checkin message should contain the restored
    // documents instead of being an empty checkin.
    std::this_thread::sleep_for( std::chrono::milliseconds( checkinIntervalMs * 2 ) );
    ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), -1 );

    EXPECT_CALL( *schemaListenerMock, mockedSendCheckin( _, _ ) ).WillRepeatedly( InvokeArgument<1>( false ) );

    ASSERT_TRUE( collectionSchemeManager.connect() );

    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 0 );

    /* build DMs */
    auto testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    collectionSchemeManager.mDmTest = testDM1;
    collectionSchemeManager.myInvokeDecoderManifest();

    WAIT_ASSERT_EQ( schemaListenerMock->getLastSentDocuments( sentDocuments ), 1 );
    ASSERT_EQ( sentDocuments[0], "DM1" );
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
TEST_F( CollectionSchemeManagerTest, ReceiveCollectionSchemeUpdateWithExistingSchemes )
{
    auto testDM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    mCollectionSchemeManager.mDmTest = testDM1;
    mCollectionSchemeManager.myInvokeDecoderManifest();
    ASSERT_TRUE( mCollectionSchemeManager.connect() );

    WAIT_ASSERT_EQ( mReceivedInspectionMatrices.size(), 1U );

    auto currentTimeMs = mTestClock->timeSinceEpoch().systemTimeMs;
    auto startTimeInNearFuture = currentTimeMs + 500;
    auto stopTimeInDistantFuture = currentTimeMs + 200000;
    auto startTimeInThePast = currentTimeMs - 1000;
    std::vector<SignalCollectionInfo> signals1 = { SignalCollectionInfo{ 0xFFFF0000 },
                                                   SignalCollectionInfo{ 0xFFFF0001 } };
    ICollectionScheme::PartialSignalIDLookup partialSignalIDLookup1{
        { 0xFFFF0000, std::pair<SignalID, SignalPath>( 0x2000000, SignalPath{ 1, 2, 5 } ) },
        { 0xFFFF0001, std::pair<SignalID, SignalPath>( 0x2000000, SignalPath{ 1, 1, 3 } ) } };
    auto idleCollectionScheme = std::make_shared<ICollectionSchemeTest>( "IdleCollectionScheme",
                                                                         "DM1",
                                                                         startTimeInNearFuture,
                                                                         stopTimeInDistantFuture,
                                                                         signals1,
                                                                         partialSignalIDLookup1 );

    std::vector<SignalCollectionInfo> signals2 = { SignalCollectionInfo{ 0xFFFF0002 },
                                                   SignalCollectionInfo{ 0xFFFF0003 } };
    ICollectionScheme::PartialSignalIDLookup partialSignalIDLookup2{
        { 0xFFFF0002, std::pair<SignalID, SignalPath>( 0x2000001, SignalPath{ 1, 2, 5 } ) },
        { 0xFFFF0003, std::pair<SignalID, SignalPath>( 0x2000001, SignalPath{ 1, 1, 3 } ) } };
    auto enabledCollectionScheme = std::make_shared<ICollectionSchemeTest>( "EnabledCollectionScheme",
                                                                            "DM1",
                                                                            startTimeInThePast,
                                                                            stopTimeInDistantFuture,
                                                                            signals2,
                                                                            partialSignalIDLookup2 );

    mCollectionSchemeManager.onCollectionSchemeUpdate( std::make_shared<ICollectionSchemeListTest>(
        std::vector<std::shared_ptr<ICollectionScheme>>{ idleCollectionScheme, enabledCollectionScheme } ) );

    WAIT_ASSERT_EQ( mReceivedInspectionMatrices.size(), 2U );

    // Repeat the same collection schemes, but modify the signals for the idle one. This is to make
    // sure that later when CollectionSchemeManager enables this collection scheme, it will use the
    // new signal IDs.
    signals1 = { SignalCollectionInfo{ 0xFFFF0010 }, SignalCollectionInfo{ 0xFFFF0011 } };
    partialSignalIDLookup1 = { { 0xFFFF0010, std::pair<SignalID, SignalPath>( 0x2000000, SignalPath{ 1, 2, 5 } ) },
                               { 0xFFFF0011, std::pair<SignalID, SignalPath>( 0x2000000, SignalPath{ 1, 1, 3 } ) } };
    idleCollectionScheme = std::make_shared<ICollectionSchemeTest>( "IdleCollectionScheme",
                                                                    "DM1",
                                                                    startTimeInNearFuture,
                                                                    stopTimeInDistantFuture,
                                                                    signals1,
                                                                    partialSignalIDLookup1 );

    enabledCollectionScheme = std::make_shared<ICollectionSchemeTest>( "EnabledCollectionScheme",
                                                                       "DM1",
                                                                       startTimeInThePast,
                                                                       stopTimeInDistantFuture,
                                                                       signals2,
                                                                       partialSignalIDLookup2 );
    mCollectionSchemeManager.onCollectionSchemeUpdate( std::make_shared<ICollectionSchemeListTest>(
        std::vector<std::shared_ptr<ICollectionScheme>>{ enabledCollectionScheme, idleCollectionScheme } ) );

    WAIT_ASSERT_EQ( mReceivedInspectionMatrices.size(), 3U );

    // Now send the same collection schemes, but slightly modified. In general collection schemes
    // coming from the Cloud with the same ID should be considered the same. But for complex data
    // some the partial signal IDs are generated by us and they can change when we ingest a new
    // message from the Cloud.
    idleCollectionScheme =
        std::make_shared<ICollectionSchemeTest>( "IdleCollectionScheme",
                                                 "DM1",
                                                 // Also make the idle collection scheme become enabled
                                                 startTimeInNearFuture,
                                                 stopTimeInDistantFuture,
                                                 signals1,
                                                 partialSignalIDLookup1 );

    signals2 = { SignalCollectionInfo{ 0xFFFF0012 }, SignalCollectionInfo{ 0xFFFF0013 } };
    partialSignalIDLookup2 = { { 0xFFFF0012, std::pair<SignalID, SignalPath>( 0x2000001, SignalPath{ 1, 2, 5 } ) },
                               { 0xFFFF0013, std::pair<SignalID, SignalPath>( 0x2000001, SignalPath{ 1, 1, 3 } ) } };
    enabledCollectionScheme = std::make_shared<ICollectionSchemeTest>( "EnabledCollectionScheme",
                                                                       "DM1",
                                                                       startTimeInThePast,
                                                                       stopTimeInDistantFuture,
                                                                       signals2,
                                                                       partialSignalIDLookup2 );
    mCollectionSchemeManager.onCollectionSchemeUpdate( std::make_shared<ICollectionSchemeListTest>(
        std::vector<std::shared_ptr<ICollectionScheme>>{ enabledCollectionScheme, idleCollectionScheme } ) );

    WAIT_ASSERT_EQ( mReceivedInspectionMatrices.size(), 4U );

    auto lastReceivedInspectionMatrix = mReceivedInspectionMatrices.at( 3 );
    ASSERT_EQ( lastReceivedInspectionMatrix->conditions.size(), 2U );

    ASSERT_EQ( getSignalIdsFromCondition( lastReceivedInspectionMatrix->conditions.at( 0 ) ),
               ( std::vector<SignalID>{ 0xFFFF0012, 0xFFFF0013 } ) );
    ASSERT_EQ( getSignalIdsFromCondition( lastReceivedInspectionMatrix->conditions.at( 1 ) ),
               ( std::vector<SignalID>{ 0xFFFF0010, 0xFFFF0011 } ) );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
