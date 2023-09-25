// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CANInterfaceIDTranslator.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include <cstdio>
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{

/** @brief
 * This test aims to test PM's functionality to invoke Inspection Engine on Inspection Matrix Update
 * step1:
 * Two Inspection Engine registered as listener
 * step2: Invoke Inspection Matrix Update
 * check Both two Inspection Engines has Inspection Matrix Update
 */
TEST( CollectionSchemeManagerTest, InspectionMatrixUpdaterTest )
{
    auto testPtr = std::make_shared<CollectionSchemeManagerTest>();
    CANInterfaceIDTranslator canIDTranslator;
    testPtr->init( 0, nullptr, canIDTranslator );

    // Mock two Inspection Engine Mock
    std::shared_ptr<CollectionInspectionEngineMock> InspectionEnginePtr;
    std::shared_ptr<CollectionInspectionEngineMock> InspectionEnginePtr2;
    InspectionEnginePtr.reset( new CollectionInspectionEngineMock() );
    InspectionEnginePtr2.reset( new CollectionInspectionEngineMock() );

    testPtr->subscribeListener( InspectionEnginePtr.get() );
    testPtr->subscribeListener( InspectionEnginePtr2.get() );

    // Clear updater flag. Note this flag only exist in mock class for testing purpose
    InspectionEnginePtr->setUpdateFlag( false );
    InspectionEnginePtr2->setUpdateFlag( false );
    // Invoke the updater
    testPtr->inspectionMatrixUpdater( std::make_shared<InspectionMatrix>() );
    // Check both two consumers has updater flag set
    ASSERT_TRUE( InspectionEnginePtr->getUpdateFlag() );
    ASSERT_TRUE( InspectionEnginePtr2->getUpdateFlag() );
}

ExpressionNode *
buildTree( int startVal, int count )
{
    ExpressionNode *root = new ExpressionNode();
    root->floatingValue = startVal;
    std::queue<ExpressionNode *> nodeQ;
    nodeQ.push( root );
    count--;
    ExpressionNode *currNode;

    while ( count )
    {
        currNode = nodeQ.front();
        nodeQ.pop();
        startVal++;
        currNode->left = new ExpressionNode();
        currNode->left->floatingValue = startVal;
        nodeQ.push( currNode->left );
        startVal++;
        count--;
        if ( count == 0 )
        {
            break;
        }
        currNode->right = new ExpressionNode();
        currNode->right->floatingValue = startVal;
        nodeQ.push( currNode->right );
        count--;
    }
    return root;
}

void
printAndVerifyTree( const ExpressionNode *root )
{
    std::queue<const ExpressionNode *> nodeQ;
    nodeQ.push( root );
    const ExpressionNode *currNode;
    double val = root->floatingValue;

    while ( !nodeQ.empty() )
    {
        currNode = nodeQ.front();
        nodeQ.pop();
        ASSERT_EQ( val, currNode->floatingValue );
        val += 1;
        printf( "%4d", static_cast<uint32_t>( currNode->floatingValue ) );
        if ( currNode->left )
        {
            nodeQ.push( currNode->left );
        }
        if ( currNode->right )
        {
            nodeQ.push( currNode->right );
        }
    }
    printf( "\n" );
}

void
deleteTree( ExpressionNode *root )
{
    std::queue<ExpressionNode *> nodeQ;
    nodeQ.push( root );
    ExpressionNode *currNode;

    while ( !nodeQ.empty() )
    {
        currNode = nodeQ.front();
        nodeQ.pop();
        if ( currNode->left )
        {
            nodeQ.push( currNode->left );
        }
        if ( currNode->right )
        {
            nodeQ.push( currNode->right );
        }
        delete ( currNode );
    }
    printf( "\n" );
}
/** @brief
 * This test mock 3 collectionSchemes, each with a binary tree of 20 nodes.
 * Calls function inspectionMatrixExtractor
 * Exam the output: printout flatten trees as well as traverse each tree
 * using ConditionWithCollectedData.condition
 *
 */
TEST( CollectionSchemeManager, InpsectionMatrixExtractorTreeTest )
{

    /* construct trees */
    ExpressionNode *tree1, *tree2, *tree3;
    tree1 = buildTree( 1, 20 );
    tree2 = buildTree( 11, 20 );
    tree3 = buildTree( 21, 20 );
    ICollectionSchemePtr collectionScheme1 =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", 0, 10, tree1 );
    ICollectionSchemePtr collectionScheme2 =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME2", "DM1", 0, 10, tree2 );
    ICollectionSchemePtr collectionScheme3 =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME3", "DM1", 0, 10, tree3 );
    std::vector<ICollectionSchemePtr> list1;
    list1.emplace_back( collectionScheme1 );
    list1.emplace_back( collectionScheme2 );
    list1.emplace_back( collectionScheme3 );

    CollectionSchemeManagerTest test( "DM1" );
    IDecoderManifestPtr DM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DM1 );
    test.setCollectionSchemeList( PL1 );
    // All three polices are expected to be enabled
    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();
    test.inspectionMatrixExtractor( output );

    /* exam output */
    for ( uint32_t i = 0; i < output->expressionNodeStorage.size(); i++ )
    {
        if ( i % 10 == 0 )
        {
            printf( "\n\n" );
        }
        printf( "%4d ", static_cast<uint32_t>( output->expressionNodeStorage[i].floatingValue ) );
    }
    printf( "\nPrinting and verifying trees pre-order:\n" );
    for ( uint32_t i = 0; i < output->conditions.size(); i++ )
    {
        const ExpressionNode *root = output->conditions[i].condition;
        printAndVerifyTree( root );
    }
    deleteTree( tree1 );
    deleteTree( tree2 );
    deleteTree( tree3 );
}

TEST( CollectionSchemeManager, InpsectionMatrixExtractorConditionDataTest )
{
    struct SignalCollectionInfo Signals;
    Signals.signalID = 1;
    Signals.sampleBufferSize = 2;
    Signals.minimumSampleIntervalMs = 3;
    Signals.fixedWindowPeriod = 4;
    struct CanFrameCollectionInfo CANFrame;
    CANFrame.frameID = 101;
    CANFrame.interfaceID = "102";
    CANFrame.sampleBufferSize = 103;
    CANFrame.minimumSampleIntervalMs = 104;
    std::vector<SignalCollectionInfo> testSignals = { Signals };
    std::vector<CanFrameCollectionInfo> testCANFrames = { CANFrame };
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", 0, 10, testSignals, testCANFrames );
    std::vector<ICollectionSchemePtr> list1;
    list1.emplace_back( collectionScheme );
    CollectionSchemeManagerTest test( "DM1" );
    CANInterfaceIDTranslator canIDTranslator;
    canIDTranslator.add( "102" );
    test.init( 0, NULL, canIDTranslator );
    IDecoderManifestPtr DM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DM1 );
    test.setCollectionSchemeList( PL1 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();
    test.inspectionMatrixExtractor( output );
    for ( auto conditionData : output->conditions )
    {
        // Signals
        ASSERT_EQ( conditionData.signals.size(), 1 );
        ASSERT_EQ( Signals.signalID, conditionData.signals[0].signalID );
        ASSERT_EQ( Signals.sampleBufferSize, conditionData.signals[0].sampleBufferSize );
        ASSERT_EQ( Signals.minimumSampleIntervalMs, conditionData.signals[0].minimumSampleIntervalMs );
        ASSERT_EQ( Signals.fixedWindowPeriod, conditionData.signals[0].fixedWindowPeriod );
        ASSERT_EQ( Signals.isConditionOnlySignal, conditionData.signals[0].isConditionOnlySignal );
        // Raw Frames
        ASSERT_EQ( conditionData.canFrames.size(), 1 );
        ASSERT_EQ( CANFrame.frameID, conditionData.canFrames[0].frameID );
        ASSERT_EQ( CANFrame.interfaceID, canIDTranslator.getInterfaceID( conditionData.canFrames[0].channelID ) );
        ASSERT_EQ( CANFrame.sampleBufferSize, conditionData.canFrames[0].sampleBufferSize );
        ASSERT_EQ( CANFrame.minimumSampleIntervalMs, conditionData.canFrames[0].minimumSampleIntervalMs );
        // Decoder and CollectionScheme IDs
        ASSERT_EQ( conditionData.metadata.decoderID, collectionScheme->getDecoderManifestID() );
        ASSERT_EQ( conditionData.metadata.collectionSchemeID, collectionScheme->getCollectionSchemeID() );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
