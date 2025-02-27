// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManagerMock.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include "RawDataBufferManagerSpy.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include <boost/optional/optional.hpp>
#include <cstdio>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <queue>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "aws/iotfleetwise/IDecoderDictionary.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::NiceMock;

/** @brief
 * This test aims to test PM's functionality to invoke Inspection Engine on Inspection Matrix and Fetch Matrix update
 * Step1: Register two Inspection Engines are listeners
 * Step2: Invoke Matrix Updater and check if two Inspection Engines receive Inspection Matrix and Fetch Matrix update
 */
TEST( InspectionMatrixExtractorTest, MatrixUpdaterTest )
{
    CANInterfaceIDTranslator canIDTranslator;
    auto testPtr = std::make_shared<CollectionSchemeManagerWrapper>(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ) );

    // Mock two Inspection Engine Mock
    CollectionInspectionWorkerThreadMock workerThread1;
    CollectionInspectionWorkerThreadMock workerThread2;

    // Register two Inspection Engines as listeners to Inspection Matrix update
    testPtr->subscribeToInspectionMatrixChange( std::bind(
        &CollectionInspectionWorkerThreadMock::onChangeInspectionMatrix, &workerThread1, std::placeholders::_1 ) );
    testPtr->subscribeToInspectionMatrixChange( std::bind(
        &CollectionInspectionWorkerThreadMock::onChangeInspectionMatrix, &workerThread2, std::placeholders::_1 ) );

    // Clear Inspection Matrix update flag (this flag only exist in mock class for testing purpose)
    workerThread1.setInspectionMatrixUpdateFlag( false );
    workerThread2.setInspectionMatrixUpdateFlag( false );

    // Invoke Inspection Matrix updater
    testPtr->inspectionMatrixUpdater( std::make_shared<InspectionMatrix>() );

    // Check if both two consumers set Inspection Matrix update flag
    ASSERT_TRUE( workerThread1.getInspectionMatrixUpdateFlag() );
    ASSERT_TRUE( workerThread2.getInspectionMatrixUpdateFlag() );

    // Register Inspection Engine #1 as listeners to Fetch Matrix update
    testPtr->subscribeToFetchMatrixChange( std::bind(
        &CollectionInspectionWorkerThreadMock::onChangeFetchMatrix, &workerThread1, std::placeholders::_1 ) );

    // Clear Fetch Matrix update flag (this flag only exist in mock class for testing purpose)
    workerThread1.setFetchMatrixUpdateFlag( false );
    workerThread2.setFetchMatrixUpdateFlag( false );

    // Invoke Fetch Matrix updater
    testPtr->fetchMatrixUpdater( std::make_shared<FetchMatrix>() );

    // Check if both two consumers set Fetch Matrix update flag appropriately
    // Only Inspection Engine #1 should receive Fetch Matrix update
    ASSERT_TRUE( workerThread1.getFetchMatrixUpdateFlag() );
    ASSERT_FALSE( workerThread2.getFetchMatrixUpdateFlag() );

    // Register Inspection Engine #2 as listeners to Fetch Matrix update, too
    testPtr->subscribeToFetchMatrixChange( std::bind(
        &CollectionInspectionWorkerThreadMock::onChangeFetchMatrix, &workerThread2, std::placeholders::_1 ) );

    // Clear Fetch Matrix update flag (this flag only exist in mock class for testing purpose)
    workerThread1.setFetchMatrixUpdateFlag( false );
    workerThread2.setFetchMatrixUpdateFlag( false );

    // Invoke Fetch Matrix updater
    testPtr->fetchMatrixUpdater( std::make_shared<FetchMatrix>() );

    // Check if both two consumers set Fetch Matrix update flag appropriately
    // Both Inspection Engines should receive Fetch Matrix update
    ASSERT_TRUE( workerThread1.getFetchMatrixUpdateFlag() );
    ASSERT_TRUE( workerThread2.getFetchMatrixUpdateFlag() );
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
 * Calls function matrixExtractor
 * Exam the output: printout flatten trees as well as traverse each tree
 * using ConditionWithCollectedData.condition
 *
 */
TEST( CollectionSchemeManager, InspectionMatrixExtractorTreeTest )
{

    /* construct trees */
    ExpressionNode *tree1 = buildTree( 1, 20 );
    ExpressionNode *tree2 = buildTree( 11, 20 );
    ExpressionNode *tree3 = buildTree( 21, 20 );
    auto collectionScheme1 = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", 0, 10, tree1 );
    auto collectionScheme2 = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME2", "DM1", 0, 10, tree2 );
    auto collectionScheme3 = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME3", "DM1", 0, 10, tree3 );
    std::vector<std::shared_ptr<ICollectionScheme>> list1;
    list1.emplace_back( collectionScheme1 );
    list1.emplace_back( collectionScheme2 );
    list1.emplace_back( collectionScheme3 );

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper test( nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DM1" );
    IDecoderManifestPtr DM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DM1 );
    test.setCollectionSchemeList( PL1 );
    // All three polices are expected to be enabled
    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    InspectionMatrix output;
    FetchMatrix fetchMatrixOutput;
    test.matrixExtractor( output, fetchMatrixOutput );

    /* exam output */
    for ( uint32_t i = 0; i < output.expressionNodeStorage.size(); i++ )
    {
        if ( i % 10 == 0 )
        {
            printf( "\n\n" );
        }
        printf( "%4d ", static_cast<uint32_t>( output.expressionNodeStorage[i].floatingValue ) );
    }
    printf( "\nPrinting and verifying trees pre-order:\n" );
    for ( uint32_t i = 0; i < output.conditions.size(); i++ )
    {
        const ExpressionNode *root = output.conditions[i].condition;
        printAndVerifyTree( root );
    }
    deleteTree( tree1 );
    deleteTree( tree2 );
    deleteTree( tree3 );
}

TEST( CollectionSchemeManager, InspectionMatrixExtractorConditionDataTest )
{
    struct SignalCollectionInfo Signals;
    Signals.signalID = 1;
    Signals.sampleBufferSize = 2;
    Signals.minimumSampleIntervalMs = 3;
    Signals.fixedWindowPeriod = 4;
    std::vector<SignalCollectionInfo> testSignals = { Signals };
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DM1", 0, 10, testSignals );
    std::vector<std::shared_ptr<ICollectionScheme>> list1;
    list1.emplace_back( collectionScheme );
    CANInterfaceIDTranslator canIDTranslator;
    canIDTranslator.add( "102" );
    CollectionSchemeManagerWrapper test( nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DM1" );
    IDecoderManifestPtr DM1 = std::make_shared<IDecoderManifestTest>( "DM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DM1 );
    test.setCollectionSchemeList( PL1 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    InspectionMatrix output;
    FetchMatrix fetchMatrixOutput;
    test.matrixExtractor( output, fetchMatrixOutput );
    for ( auto conditionData : output.conditions )
    {
        // Signals
        ASSERT_EQ( conditionData.signals.size(), 1 );
        ASSERT_EQ( Signals.signalID, conditionData.signals[0].signalID );
        ASSERT_EQ( Signals.sampleBufferSize, conditionData.signals[0].sampleBufferSize );
        ASSERT_EQ( Signals.minimumSampleIntervalMs, conditionData.signals[0].minimumSampleIntervalMs );
        ASSERT_EQ( Signals.fixedWindowPeriod, conditionData.signals[0].fixedWindowPeriod );
        ASSERT_EQ( Signals.isConditionOnlySignal, conditionData.signals[0].isConditionOnlySignal );
        // Decoder and CollectionScheme IDs
        ASSERT_EQ( conditionData.metadata.decoderID, collectionScheme->getDecoderManifestID() );
        ASSERT_EQ( conditionData.metadata.collectionSchemeID, collectionScheme->getCollectionSchemeID() );
    }
}

static void
addSignalCollectionInfo( std::vector<SignalCollectionInfo> &collectSignals,
                         SignalID signalID,
                         uint32_t sampleBufferSize,
                         uint32_t minimumSamplePeriodMs,
                         uint32_t fixedWindowPeriodMs,
                         bool conditionOnlySignal )
{
    collectSignals.emplace_back();

    SignalCollectionInfo &collectSignal = collectSignals.back();

    collectSignal.signalID = signalID;
    collectSignal.sampleBufferSize = sampleBufferSize;
    collectSignal.minimumSampleIntervalMs = minimumSamplePeriodMs;
    collectSignal.fixedWindowPeriod = fixedWindowPeriodMs;
    collectSignal.isConditionOnlySignal = conditionOnlySignal;
}

static void
addFetchInformation( std::vector<FetchInformation> &fetchInformations,
                     std::shared_ptr<std::vector<ExpressionNode>> nodesForFetchCondition,
                     std::shared_ptr<std::vector<ExpressionNode>> nodesForFetchAction,
                     SignalID signalID,
                     bool isTimeBased,
                     uint64_t maxExecutionCount,
                     uint64_t executionFrequencyMs,
                     uint64_t resetMaxExecutionCountIntervalMs,
                     bool isActionValid,
                     bool isActionParamValid,
                     ExpressionNodeType nodeType )
{
    fetchInformations.emplace_back();

    FetchInformation &fetchInformation = fetchInformations.back();

    fetchInformation.signalID = signalID;

    if ( isTimeBased )
    {
        fetchInformation.maxExecutionPerInterval = maxExecutionCount;
        fetchInformation.executionPeriodMs = executionFrequencyMs;
        fetchInformation.executionIntervalMs = resetMaxExecutionCountIntervalMs;
    }
    else
    {
        nodesForFetchCondition->emplace_back();

        ExpressionNode &fetchInformationCondition = nodesForFetchCondition->back();

        fetchInformation.condition = &fetchInformationCondition;
        fetchInformation.triggerOnlyOnRisingEdge = true;

        fetchInformationCondition.nodeType = ExpressionNodeType::SIGNAL;
        fetchInformationCondition.signalID = signalID;
    }

    nodesForFetchAction->emplace_back();

    ExpressionNode &fetchInformationAction = nodesForFetchAction->back();

    fetchInformation.actions.push_back( &fetchInformationAction );

    if ( isActionValid )
    {
        // action valid per inspection matrix extractor only (not guarantee to be valid through whole system)
        fetchInformationAction.nodeType = ExpressionNodeType::CUSTOM_FUNCTION;
        fetchInformationAction.function.customFunctionName = "Custom_Function_Name";

        nodesForFetchAction->emplace_back();

        ExpressionNode &customFunctionParam = nodesForFetchAction->back();

        fetchInformationAction.function.customFunctionParams.push_back( &customFunctionParam );

        if ( isActionParamValid )
        {
            switch ( nodeType )
            {
            case ExpressionNodeType::BOOLEAN:
                customFunctionParam.nodeType = ExpressionNodeType::BOOLEAN;
                customFunctionParam.booleanValue = true;
                break;
            case ExpressionNodeType::FLOAT:
                customFunctionParam.nodeType = ExpressionNodeType::FLOAT;
                customFunctionParam.floatingValue = 1.0;
                break;
            case ExpressionNodeType::STRING:
                customFunctionParam.nodeType = ExpressionNodeType::STRING;
                customFunctionParam.stringValue = "test_string";
                break;
            default:
                customFunctionParam.nodeType = ExpressionNodeType::NONE;
                break;
            }
        }
        else
        {
            customFunctionParam.nodeType = ExpressionNodeType::NONE;
        }
    }
    else
    {
        fetchInformationAction.nodeType = ExpressionNodeType::NONE;
    }
}

TEST( CollectionSchemeManager, MatrixExtractorTest )
{
    std::vector<SignalCollectionInfo> collectSignals;
    std::vector<FetchInformation> fetchInformations;
    ExpressionNode *tree = new ExpressionNode();
    tree->nodeType = ExpressionNodeType::IS_NULL_FUNCTION;
    tree->left = new ExpressionNode();
    tree->left->nodeType = ExpressionNodeType::SIGNAL;
    tree->left->signalID = 1;

    std::shared_ptr<std::vector<ExpressionNode>> nodesForFetchCondition =
        std::make_shared<std::vector<ExpressionNode>>();
    std::shared_ptr<std::vector<ExpressionNode>> nodesForFetchAction = std::make_shared<std::vector<ExpressionNode>>();

    const SignalID signalID1 = 10;
    const SignalID signalID2 = 20;
    const SignalID signalID3 = 30;

    addSignalCollectionInfo( collectSignals, signalID1, 11, 12, 13, false );
    addSignalCollectionInfo( collectSignals, signalID2, 21, 22, 23, false );
    addSignalCollectionInfo( collectSignals, signalID3, 31, 32, 33, true );

    // refer to addFetchInformation implementation
    // each FetchInformations need 0-1 ExpressionNode for condition and 1-2 ExpressionNodes for actions
    // we're going to add 7 FetchInformations
    // so that we will need 0-7 ExpressionNodes for conditions and 7-14 ExpressionNodes for actions
    nodesForFetchCondition->reserve( 7 );
    nodesForFetchAction->reserve( 14 );

    // time-based fetch for signalID1 => accepted (FetchRequestID 0)
    // action is a custom function with name "Custom_Function_Name" and double parameter
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID1,
                         true,
                         1000,
                         1100,
                         1200,
                         true,
                         true,
                         ExpressionNodeType::FLOAT );
    // condition-based fetch for signalID1 => accepted (FetchRequestID 1)
    // action is a custom function with name "Custom_Function_Name" and string parameter
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID1,
                         false,
                         2000,
                         2100,
                         2200,
                         true,
                         true,
                         ExpressionNodeType::STRING );
    // time-based fetch for signalID1 => not accepted
    // action is not a custom function
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID1,
                         true,
                         3000,
                         3100,
                         3200,
                         false,
                         true,
                         ExpressionNodeType::FLOAT );
    // condition-based fetch for signalID1 => not accepted
    // action is a custom function but custom function parameter is neither bool nor double nor string value
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID1,
                         false,
                         4000,
                         4100,
                         4200,
                         true,
                         false,
                         ExpressionNodeType::NONE );
    // time-based fetch for signalID2 => accepted (FetchRequestID 2)
    // action is a custom function with name "Custom_Function_Name" and bool parameter
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID2,
                         true,
                         5000,
                         5100,
                         5200,
                         true,
                         true,
                         ExpressionNodeType::BOOLEAN );
    // condition-based fetch for signalID2 => accepted (FetchRequestID 3)
    // action is a custom function with name "Custom_Function_Name" and bool parameter
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID2,
                         false,
                         6000,
                         6100,
                         6200,
                         true,
                         true,
                         ExpressionNodeType::BOOLEAN );
    // time-based fetch for signalID1 => not accepted
    // Fetch frequency can't be 0
    addFetchInformation( fetchInformations,
                         nodesForFetchCondition,
                         nodesForFetchAction,
                         signalID1,
                         true,
                         1000,
                         0,
                         0,
                         true,
                         true,
                         ExpressionNodeType::FLOAT );

    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "CollectionScheme 1",
                                                                     "DecoderManifest 1",
                                                                     10000,
                                                                     11000,
                                                                     12000,
                                                                     13000,
                                                                     true,
                                                                     false,
                                                                     14000,
                                                                     false,
                                                                     true,
                                                                     collectSignals,
                                                                     tree,
                                                                     fetchInformations );
    std::vector<std::shared_ptr<ICollectionScheme>> collectionSchemes;

    collectionSchemes.emplace_back( collectionScheme );

    CANInterfaceIDTranslator canIDTranslator;

    canIDTranslator.add( "110" );

    CollectionSchemeManagerWrapper test(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DecoderManifest 1" );
    IDecoderManifestPtr decoderManifest = std::make_shared<IDecoderManifestTest>( "DecoderManifest 1" );
    ICollectionSchemeListPtr collectionSchemeList = std::make_shared<ICollectionSchemeListTest>( collectionSchemes );

    test.setDecoderManifest( decoderManifest );
    test.setCollectionSchemeList( collectionSchemeList );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 10000, 10000 } ) );

    InspectionMatrix inspectionMatrix;
    FetchMatrix fetchMatrix;

    test.matrixExtractor( inspectionMatrix, fetchMatrix );

    // total 4 FetchRequestIDs
    // FetchRequestID 0 is time-based and for signalID1
    // FetchRequestID 1 is condition-based and for signalID1
    // FetchRequestID 2 is time-based and for signalID2
    // FetchRequestID 3 is condition-based and for signalID2
    ASSERT_EQ( fetchMatrix.fetchRequests.size(), 4 );

    ASSERT_EQ( fetchMatrix.fetchRequests.count( 0 ), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[0].size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[0].at( 0 ).signalID, signalID1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[0].at( 0 ).functionName, "Custom_Function_Name" );
    ASSERT_EQ( fetchMatrix.fetchRequests[0].at( 0 ).args.size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[0].at( 0 ).args.at( 0 ).type, InspectionValue::DataType::DOUBLE );
    ASSERT_EQ( fetchMatrix.fetchRequests[0].at( 0 ).args.at( 0 ).doubleVal, 1.0 );

    ASSERT_EQ( fetchMatrix.fetchRequests.count( 1 ), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[1].size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[1].at( 0 ).signalID, signalID1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[1].at( 0 ).functionName, "Custom_Function_Name" );
    ASSERT_EQ( fetchMatrix.fetchRequests[1].at( 0 ).args.size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[1].at( 0 ).args.at( 0 ).type, InspectionValue::DataType::STRING );
    ASSERT_EQ( *( fetchMatrix.fetchRequests[1].at( 0 ).args.at( 0 ).stringVal ), "test_string" );

    ASSERT_EQ( fetchMatrix.fetchRequests.count( 2 ), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[2].size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[2].at( 0 ).signalID, signalID2 );
    ASSERT_EQ( fetchMatrix.fetchRequests[2].at( 0 ).functionName, "Custom_Function_Name" );
    ASSERT_EQ( fetchMatrix.fetchRequests[2].at( 0 ).args.size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[2].at( 0 ).args.at( 0 ).type, InspectionValue::DataType::BOOL );
    ASSERT_EQ( fetchMatrix.fetchRequests[2].at( 0 ).args.at( 0 ).boolVal, true );

    ASSERT_EQ( fetchMatrix.fetchRequests.count( 3 ), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[3].size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[3].at( 0 ).signalID, signalID2 );
    ASSERT_EQ( fetchMatrix.fetchRequests[3].at( 0 ).functionName, "Custom_Function_Name" );
    ASSERT_EQ( fetchMatrix.fetchRequests[3].at( 0 ).args.size(), 1 );
    ASSERT_EQ( fetchMatrix.fetchRequests[3].at( 0 ).args.at( 0 ).type, InspectionValue::DataType::BOOL );
    ASSERT_EQ( fetchMatrix.fetchRequests[3].at( 0 ).args.at( 0 ).boolVal, true );

    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup.size(), 2 );

    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup.count( 0 ), 1 );
    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup[0].maxExecutionCount, 1000 );
    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup[0].fetchFrequencyMs, 1100 );
    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup[0].maxExecutionCountResetPeriodMs, 1200 );

    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup.count( 2 ), 1 );
    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup[2].maxExecutionCount, 5000 );
    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup[2].fetchFrequencyMs, 5100 );
    ASSERT_EQ( fetchMatrix.periodicalFetchRequestSetup[2].maxExecutionCountResetPeriodMs, 5200 );

    // main condition is nullptr => contribute 0 ExpressionNode
    // FetchRequestID 1 is condition-based => contribute 1 ExpressionNode (because ExpressionNodeType::SIGNAL is used)
    // FetchRequestID 3 is condition-based => contribute 1 ExpressionNode (because ExpressionNodeType::SIGNAL is used)
    // Two more nodes for condition expression
    ASSERT_EQ( inspectionMatrix.expressionNodeStorage.size(), 4 );

    for ( auto conditionData : inspectionMatrix.conditions )
    {
        ASSERT_NE( conditionData.condition, nullptr );
        ASSERT_EQ( conditionData.minimumPublishIntervalMs, 12000 );
        ASSERT_EQ( conditionData.afterDuration, 13000 );
        ASSERT_EQ( conditionData.includeActiveDtcs, true );
        ASSERT_EQ( conditionData.triggerOnlyOnRisingEdge, false );
        ASSERT_EQ( conditionData.alwaysEvaluateCondition, true );

        // metadata
        ASSERT_EQ( conditionData.metadata.collectionSchemeID, "CollectionScheme 1" );
        ASSERT_EQ( conditionData.metadata.decoderID, "DecoderManifest 1" );
        ASSERT_EQ( conditionData.metadata.priority, 14000 );
        ASSERT_EQ( conditionData.metadata.persist, false );
        ASSERT_EQ( conditionData.metadata.compress, true );

        // signals
        ASSERT_EQ( conditionData.signals.size(), 3 );

        ASSERT_EQ( conditionData.signals[0].signalID, signalID1 );
        ASSERT_EQ( conditionData.signals[0].sampleBufferSize, 11 );
        ASSERT_EQ( conditionData.signals[0].minimumSampleIntervalMs, 12 );
        ASSERT_EQ( conditionData.signals[0].fixedWindowPeriod, 13 );
        ASSERT_EQ( conditionData.signals[0].isConditionOnlySignal, false );
        ASSERT_EQ( conditionData.signals[0].fetchRequestIDs.size(), 2 );
        ASSERT_EQ( conditionData.signals[0].fetchRequestIDs[0], 0 );
        ASSERT_EQ( conditionData.signals[0].fetchRequestIDs[1], 1 );

        ASSERT_EQ( conditionData.signals[1].signalID, signalID2 );
        ASSERT_EQ( conditionData.signals[1].sampleBufferSize, 21 );
        ASSERT_EQ( conditionData.signals[1].minimumSampleIntervalMs, 22 );
        ASSERT_EQ( conditionData.signals[1].fixedWindowPeriod, 23 );
        ASSERT_EQ( conditionData.signals[1].isConditionOnlySignal, false );
        ASSERT_EQ( conditionData.signals[1].fetchRequestIDs.size(), 2 );
        ASSERT_EQ( conditionData.signals[1].fetchRequestIDs[0], 2 );
        ASSERT_EQ( conditionData.signals[1].fetchRequestIDs[1], 3 );

        ASSERT_EQ( conditionData.signals[2].signalID, signalID3 );
        ASSERT_EQ( conditionData.signals[2].sampleBufferSize, 31 );
        ASSERT_EQ( conditionData.signals[2].minimumSampleIntervalMs, 32 );
        ASSERT_EQ( conditionData.signals[2].fixedWindowPeriod, 33 );
        ASSERT_EQ( conditionData.signals[2].isConditionOnlySignal, true );
        ASSERT_EQ( conditionData.signals[2].fetchRequestIDs.size(), 0 );

        // fetchConditions
        ASSERT_EQ( conditionData.fetchConditions.size(), 2 );

        ASSERT_EQ( conditionData.fetchConditions[0].condition, &inspectionMatrix.expressionNodeStorage[2] );
        ASSERT_EQ( conditionData.fetchConditions[0].condition->nodeType, ExpressionNodeType::SIGNAL );
        ASSERT_EQ( conditionData.fetchConditions[0].condition->signalID, signalID1 );
        ASSERT_EQ( conditionData.fetchConditions[0].triggerOnlyOnRisingEdge, true );
        ASSERT_EQ( conditionData.fetchConditions[0].fetchRequestID, 1 );

        ASSERT_EQ( conditionData.fetchConditions[1].condition, &inspectionMatrix.expressionNodeStorage[3] );
        ASSERT_EQ( conditionData.fetchConditions[1].condition->nodeType, ExpressionNodeType::SIGNAL );
        ASSERT_EQ( conditionData.fetchConditions[1].condition->signalID, signalID2 );
        ASSERT_EQ( conditionData.fetchConditions[1].triggerOnlyOnRisingEdge, true );
        ASSERT_EQ( conditionData.fetchConditions[1].fetchRequestID, 3 );
    }
    deleteTree( tree );
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
/** @brief
 * This test aims to test PM's functionality to create and update the RawBuffer Config on Inspection Matrix Update
 */
TEST( InspectionMatrixExtractorTest, InspectionMatrixRawBufferConfigUpdaterTest )
{
    struct SignalCollectionInfo signal1;
    signal1.signalID = 0x20001; // in complex data range of decoder manifest mock;
    signal1.sampleBufferSize = 2;
    signal1.minimumSampleIntervalMs = 3;
    signal1.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal2;
    signal2.signalID = 0x20002; // in complex data range of decoder manifest mock;
    signal2.sampleBufferSize = 2;
    signal2.minimumSampleIntervalMs = 3;
    signal2.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal3;
    signal3.signalID = 0x20003; // in complex data range of decoder manifest mock;
    signal3.sampleBufferSize = 3;
    signal3.minimumSampleIntervalMs = 3;
    signal3.fixedWindowPeriod = 4;
    std::vector<SignalCollectionInfo> testSignals = { signal1, signal2, signal3 };
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals );
    std::vector<std::shared_ptr<ICollectionScheme>> list1;
    list1.emplace_back( collectionScheme );

    RawData::BufferManager rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper test(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DMBM1", &rawDataBufferManager );
    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>( "DMBM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 0 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();
    std::unordered_map<RawData::BufferTypeId, Aws::IoTFleetWise::RawData::SignalUpdateConfig> updatedSignals;
    test.updateRawDataBufferConfigComplexSignals( nullptr, updatedSignals );
    // The Config should be updated and 3 Raw Data Buffer should be Allocated
    rawDataBufferManager.updateConfig( updatedSignals );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );
}

/** @brief
 * This test aims to test PM's functionality when the updated fails for the RawBuffer Config on Inspection Matrix Update
 */
TEST( InspectionMatrixExtractorTest, InspectionMatrixRawBufferConfigUpdaterMemLowTest )
{
    struct SignalCollectionInfo signal1;
    signal1.signalID = 0x20001; // in complex data range of decoder manifest mock;
    signal1.sampleBufferSize = 1;
    signal1.minimumSampleIntervalMs = 3;
    signal1.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal2;
    signal2.signalID = 0x20002; // in complex data range of decoder manifest mock;
    signal2.sampleBufferSize = 1;
    signal2.minimumSampleIntervalMs = 3;
    signal2.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal3;
    signal3.signalID = 0x20003; // in complex data range of decoder manifest mock;
    signal3.sampleBufferSize = 1;
    signal3.minimumSampleIntervalMs = 3;
    signal3.fixedWindowPeriod = 4;
    std::vector<SignalCollectionInfo> testSignals = { signal1, signal2, signal3 };
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals );
    std::vector<std::shared_ptr<ICollectionScheme>> list1;
    list1.emplace_back( collectionScheme );

    size_t maxBytes = 10000;
    size_t reservedBytesPerSignal = 5000;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    RawData::BufferManager rawDataBufferManager(
        RawData::BufferManagerConfig::create( maxBytes, reservedBytesPerSignal, {}, {}, {}, overridesPerSignal )
            .get() );

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper test(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DMBM1", &rawDataBufferManager );
    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>( "DMBM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 0 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();
    std::unordered_map<RawData::BufferTypeId, Aws::IoTFleetWise::RawData::SignalUpdateConfig> updatedSignals;
    test.updateRawDataBufferConfigComplexSignals( nullptr, updatedSignals );
    rawDataBufferManager.updateConfig( updatedSignals );
    // The Config should be updated and have only 2 Raw Data Buffer due to limited memory
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 2 );
}

TEST( InspectionMatrixExtractorTest, InspectionMatrixRawBufferConfigUpdaterWithComplexDataDictionary )
{
    struct SignalCollectionInfo signal1;
    signal1.signalID = 0x20001; // in complex data range of decoder manifest mock;
    signal1.sampleBufferSize = 2;
    signal1.minimumSampleIntervalMs = 3;
    signal1.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal2;
    signal2.signalID = 0x20002; // in complex data range of decoder manifest mock;
    signal2.sampleBufferSize = 2;
    signal2.minimumSampleIntervalMs = 3;
    signal2.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal3;
    signal3.signalID = 0x20003; // in complex data range of decoder manifest mock;
    signal3.sampleBufferSize = 3;
    signal3.minimumSampleIntervalMs = 3;
    signal3.fixedWindowPeriod = 4;

    struct SignalCollectionInfo signal4;
    // Range 0x2000-0x3000 is used for custom decoding in the unit test mock
    signal4.signalID = 0x2001;
    signal4.sampleBufferSize = 2;
    signal4.minimumSampleIntervalMs = 3;
    signal4.fixedWindowPeriod = 4;

    std::vector<SignalCollectionInfo> testSignals = { signal1, signal2, signal3, signal4 };
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals );
    std::vector<std::shared_ptr<ICollectionScheme>> list1;
    list1.emplace_back( collectionScheme );

    NiceMock<Testing::RawDataBufferManagerSpy> rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper test(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), "DMBM1", &rawDataBufferManager );

    std::unordered_map<InterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> formatMap;
    std::unordered_map<SignalID, std::pair<CANRawFrameID, InterfaceID>> signalToFrameAndNodeID;
    std::unordered_map<SignalID, PIDSignalDecoderFormat> signalIDToPIDDecoderFormat;
    SignalIDToCustomSignalDecoderFormatMap signalIDToCustomDecoderFormat = {
        { 0x2001, CustomSignalDecoderFormat{ "30", "custom-decoder-0", 0x2001, SignalType::STRING } } };
    std::unordered_map<ComplexDataTypeId, ComplexDataElement> complexDataTypeMap;
    std::unordered_map<SignalID, ComplexSignalDecoderFormat> complexSignalMap = {
        { signal1.signalID, { "interfaceId1", "ImageTopic:sensor_msgs/msg/Image", 100 } },
        { signal2.signalID, { "interfaceId2", "PointFieldTopic:sensor_msgs/msg/PointField", 200 } } };
    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>( "DMBM1",
                                                                        formatMap,
                                                                        signalToFrameAndNodeID,
                                                                        signalIDToPIDDecoderFormat,
                                                                        signalIDToCustomDecoderFormat,
                                                                        complexSignalMap,
                                                                        complexDataTypeMap );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 0 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();

    auto complexDataDictionary = std::make_shared<ComplexDataDecoderDictionary>();
    ComplexDataMessageFormat messageFormatSignal1;
    messageFormatSignal1.mSignalId = signal1.signalID;
    ComplexDataMessageFormat messageFormatSignal2;
    messageFormatSignal2.mSignalId = signal2.signalID;
    ComplexDataMessageFormat messageFormatUnknownSignal;
    messageFormatUnknownSignal.mSignalId = 99999999;

    complexDataDictionary->complexMessageDecoderMethod = {
        { "interfaceId1",
          { { "ImageTopic:sensor_msgs/msg/Image", messageFormatSignal1 },
            { "PointFieldTopic:sensor_msgs/msg/PointField", messageFormatUnknownSignal } } },
        { "interfaceId2", { { "PointFieldTopic:sensor_msgs/msg/PointField", messageFormatSignal2 } } } };

    std::unordered_map<RawData::BufferTypeId, Aws::IoTFleetWise::RawData::SignalUpdateConfig> updatedSignals;

    EXPECT_CALL( rawDataBufferManager, mockedUpdateConfig( _ ) ).WillOnce( [&updatedSignals]( auto arg ) {
        updatedSignals = arg;
        return RawData::BufferErrorCode::SUCCESSFUL;
    } );
    // Verify that the list of updatedSignals isn't overwritten
    test.updateRawDataBufferConfigComplexSignals( complexDataDictionary.get(), updatedSignals );
    test.updateRawDataBufferConfigStringSignals( updatedSignals );

    ASSERT_EQ( updatedSignals.size(), 4 );

    ASSERT_NE( updatedSignals.find( signal1.signalID ), updatedSignals.end() );
    ASSERT_EQ( updatedSignals[signal1.signalID].typeId, signal1.signalID );
    ASSERT_EQ( updatedSignals[signal1.signalID].interfaceId, "interfaceId1" );
    ASSERT_EQ( updatedSignals[signal1.signalID].messageId, "ImageTopic:sensor_msgs/msg/Image" );

    ASSERT_NE( updatedSignals.find( signal2.signalID ), updatedSignals.end() );
    ASSERT_EQ( updatedSignals[signal2.signalID].typeId, signal2.signalID );
    ASSERT_EQ( updatedSignals[signal2.signalID].interfaceId, "interfaceId2" );
    ASSERT_EQ( updatedSignals[signal2.signalID].messageId, "PointFieldTopic:sensor_msgs/msg/PointField" );

    ASSERT_NE( updatedSignals.find( signal3.signalID ), updatedSignals.end() );
    ASSERT_EQ( updatedSignals[signal3.signalID].typeId, signal3.signalID );
    // For signal3, as there were no matches in the ComplexDataDecoderDictionary, the interfaceId and messageId
    // should be empty.
    ASSERT_EQ( updatedSignals[signal3.signalID].interfaceId, "" );
    ASSERT_EQ( updatedSignals[signal3.signalID].messageId, "" );

    ASSERT_NE( updatedSignals.find( signal4.signalID ), updatedSignals.end() );
    ASSERT_EQ( updatedSignals[signal4.signalID].typeId, signal4.signalID );
    ASSERT_EQ( updatedSignals[signal4.signalID].interfaceId, "30" );
    ASSERT_EQ( updatedSignals[signal4.signalID].messageId, "custom-decoder-0" );

    rawDataBufferManager.updateConfig( updatedSignals );

    // The Config should be updated and 4 Raw Data Buffer should be Allocated
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 4 );
}
#endif

TEST( InspectionMatrixExtractorTest, InspectionMatrixRawBufferConfigUpdaterWithCustomDecodingDictionary )
{
    struct SignalCollectionInfo signal1;
    // Range 0x2000-0x3000 is used for custom decoding in the unit test mock
    signal1.signalID = 0x2001;
    signal1.sampleBufferSize = 2;
    signal1.minimumSampleIntervalMs = 3;
    signal1.fixedWindowPeriod = 4;
    struct SignalCollectionInfo signal2;
    signal2.signalID = 0x2002;
    signal2.sampleBufferSize = 2;
    signal2.minimumSampleIntervalMs = 3;
    signal2.fixedWindowPeriod = 4;
    std::vector<SignalCollectionInfo> testSignals = { signal1, signal2 };
    auto collectionScheme = std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals );
    std::vector<std::shared_ptr<ICollectionScheme>> list1;
    list1.emplace_back( collectionScheme );

    NiceMock<Testing::RawDataBufferManagerSpy> rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper test( nullptr,
                                         canIDTranslator,
                                         std::make_shared<CheckinSender>( nullptr ),
                                         "DMBM1",
                                         &rawDataBufferManager
#ifdef FWE_FEATURE_REMOTE_COMMANDS
                                         ,
                                         []() {
                                             return std::unordered_map<InterfaceID, std::vector<std::string>>{
                                                 { "30", { "custom-decoder-0", "custom-decoder-1" } } };
                                         }
#endif
    );

    std::unordered_map<InterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> formatMap;
    std::unordered_map<SignalID, std::pair<CANRawFrameID, InterfaceID>> signalToFrameAndNodeID;
    std::unordered_map<SignalID, PIDSignalDecoderFormat> signalIDToPIDDecoderFormat;
    SignalIDToCustomSignalDecoderFormatMap signalIDToCustomDecoderFormat = {
        { 0x2001, CustomSignalDecoderFormat{ "30", "custom-decoder-0", 0x2001, SignalType::STRING } },
        { 0x2002, CustomSignalDecoderFormat{ "30", "custom-decoder-1", 0x2002, SignalType::DOUBLE } } };

    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>(
        "DMBM1", formatMap, signalToFrameAndNodeID, signalIDToPIDDecoderFormat, signalIDToCustomDecoderFormat );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 0 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();

    std::unordered_map<RawData::BufferTypeId, Aws::IoTFleetWise::RawData::SignalUpdateConfig> updatedSignals;

    EXPECT_CALL( rawDataBufferManager, mockedUpdateConfig( _ ) ).WillOnce( [&updatedSignals]( auto arg ) {
        updatedSignals = arg;
        return RawData::BufferErrorCode::SUCCESSFUL;
    } );
    test.updateRawDataBufferConfigStringSignals( updatedSignals );

    // Only string signal should be used for update
    ASSERT_EQ( updatedSignals.size(), 1 );

    ASSERT_NE( updatedSignals.find( signal1.signalID ), updatedSignals.end() );
    ASSERT_EQ( updatedSignals[signal1.signalID].typeId, signal1.signalID );
    ASSERT_EQ( updatedSignals[signal1.signalID].interfaceId, "30" );
    ASSERT_EQ( updatedSignals[signal1.signalID].messageId, "custom-decoder-0" );

    rawDataBufferManager.updateConfig( updatedSignals );

    // The Config should be updated and 1 Raw Data Buffer should be Allocated
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
