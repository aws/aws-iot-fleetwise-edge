// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CANInterfaceIDTranslator.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include <cstdio>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "RawDataBufferManagerSpy.h"
#include "RawDataManager.h"
#include <boost/optional/optional.hpp>
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::NiceMock;

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
    std::shared_ptr<CollectionInspectionWorkerThreadMock> InspectionEnginePtr;
    std::shared_ptr<CollectionInspectionWorkerThreadMock> InspectionEnginePtr2;
    InspectionEnginePtr.reset( new CollectionInspectionWorkerThreadMock() );
    InspectionEnginePtr2.reset( new CollectionInspectionWorkerThreadMock() );

    testPtr->subscribeToInspectionMatrixChange(
        std::bind( &CollectionInspectionWorkerThreadMock::onChangeInspectionMatrix,
                   InspectionEnginePtr.get(),
                   std::placeholders::_1 ) );
    testPtr->subscribeToInspectionMatrixChange(
        std::bind( &CollectionInspectionWorkerThreadMock::onChangeInspectionMatrix,
                   InspectionEnginePtr2.get(),
                   std::placeholders::_1 ) );

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
TEST( CollectionSchemeManager, InspectionMatrixExtractorTreeTest )
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

TEST( CollectionSchemeManager, InspectionMatrixExtractorConditionDataTest )
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
    test.init( 0, nullptr, canIDTranslator );
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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
/** @brief
 * This test aims to test PM's functionality to create and update the RawBuffer Config on Inspection Matrix Update
 */
TEST( CollectionSchemeManagerTest, InspectionMatrixRawBufferConfigUpdaterTest )
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
    std::vector<CanFrameCollectionInfo> testCANFrames = {};
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals, testCANFrames );
    std::vector<ICollectionSchemePtr> list1;
    list1.emplace_back( collectionScheme );

    // Create a Raw Data Buffer Manager
    auto rawDataBufferManager =
        std::make_shared<RawData::BufferManager>( RawData::BufferManagerConfig::create().get() );

    CollectionSchemeManagerTest test( "DMBM1" );
    CANInterfaceIDTranslator canIDTranslator;
    test.init( 0, nullptr, canIDTranslator, rawDataBufferManager );
    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>( "DMBM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager->getActiveBuffers(), 0 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();
    test.updateRawDataBufferConfig( nullptr );
    // The Config should be updated and 3 Raw Data Buffer should be Allocated
    ASSERT_EQ( rawDataBufferManager->getActiveBuffers(), 3 );
}

/** @brief
 * This test aims to test PM's functionality when the updated fails for the RawBuffer Config on Inspection Matrix Update
 */
TEST( CollectionSchemeManagerTest, InspectionMatrixRawBufferConfigUpdaterMemLowTest )
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
    std::vector<CanFrameCollectionInfo> testCANFrames = {};
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals, testCANFrames );
    std::vector<ICollectionSchemePtr> list1;
    list1.emplace_back( collectionScheme );

    size_t maxBytes = 10000;
    size_t reservedBytesPerSignal = 5000;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    auto rawDataBufferManager = std::make_shared<RawData::BufferManager>(
        RawData::BufferManagerConfig::create( maxBytes, reservedBytesPerSignal, {}, {}, {}, overridesPerSignal )
            .get() );

    CollectionSchemeManagerTest test( "DMBM1" );
    CANInterfaceIDTranslator canIDTranslator;
    test.init( 0, nullptr, canIDTranslator, rawDataBufferManager );
    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>( "DMBM1" );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager->getActiveBuffers(), 0 );

    ASSERT_TRUE( test.updateMapsandTimeLine( { 0, 0 } ) );
    std::shared_ptr<InspectionMatrix> output = std::make_shared<struct InspectionMatrix>();
    test.updateRawDataBufferConfig( nullptr );
    // The Config should be updated and have only 2 Raw Data Buffer due to limited memory
    ASSERT_EQ( rawDataBufferManager->getActiveBuffers(), 2 );
}

TEST( CollectionSchemeManagerTest, InspectionMatrixRawBufferConfigUpdaterWithComplexDataDictionary )
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
    std::vector<CanFrameCollectionInfo> testCANFrames = {};
    ICollectionSchemePtr collectionScheme =
        std::make_shared<ICollectionSchemeTest>( "COLLECTIONSCHEME1", "DMBM1", 0, 10, testSignals, testCANFrames );
    std::vector<ICollectionSchemePtr> list1;
    list1.emplace_back( collectionScheme );

    // Create a Raw Data Buffer Manager
    auto rawDataBufferManager =
        std::make_shared<NiceMock<Testing::RawDataBufferManagerSpy>>( RawData::BufferManagerConfig::create().get() );

    CollectionSchemeManagerTest test( "DMBM1" );
    CANInterfaceIDTranslator canIDTranslator;
    test.init( 0, nullptr, canIDTranslator, rawDataBufferManager );

    std::unordered_map<InterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> formatMap;
    std::unordered_map<SignalID, std::pair<CANRawFrameID, InterfaceID>> signalToFrameAndNodeID;
    std::unordered_map<SignalID, PIDSignalDecoderFormat> signalIDToPIDDecoderFormat;
    std::unordered_map<ComplexDataTypeId, ComplexDataElement> complexDataTypeMap;
    std::unordered_map<SignalID, ComplexSignalDecoderFormat> complexSignalMap = {
        { signal1.signalID, { "interfaceId1", "ImageTopic:sensor_msgs/msg/Image", 100 } },
        { signal2.signalID, { "interfaceId2", "PointFieldTopic:sensor_msgs/msg/PointField", 200 } } };
    IDecoderManifestPtr DMBM1 = std::make_shared<IDecoderManifestTest>(
        "DMBM1", formatMap, signalToFrameAndNodeID, signalIDToPIDDecoderFormat, complexSignalMap, complexDataTypeMap );
    ICollectionSchemeListPtr PL1 = std::make_shared<ICollectionSchemeListTest>( list1 );
    test.setDecoderManifest( DMBM1 );
    test.setCollectionSchemeList( PL1 );

    // Config not set so no Buffer should be allocated
    ASSERT_EQ( rawDataBufferManager->getActiveBuffers(), 0 );

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

    EXPECT_CALL( *rawDataBufferManager, mockedUpdateConfig( _ ) ).WillOnce( [&updatedSignals]( auto arg ) {
        updatedSignals = arg;
        return RawData::BufferErrorCode::SUCCESSFUL;
    } );
    test.updateRawDataBufferConfig( complexDataDictionary );

    ASSERT_EQ( updatedSignals.size(), 3 );

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

    // The Config should be updated and 3 Raw Data Buffer should be Allocated
    ASSERT_EQ( rawDataBufferManager->getActiveBuffers(), 3 );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
