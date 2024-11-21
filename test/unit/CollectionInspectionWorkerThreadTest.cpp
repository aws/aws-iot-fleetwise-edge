// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionInspectionWorkerThread.h"
#include "CANDataTypes.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionInspectionEngine.h"
#include "DataSenderTypes.h"
#include "ICollectionScheme.h"
#include "OBDDataTypes.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "WaitUntil.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "CANInterfaceIDTranslator.h"
#include "DataSenderProtoWriter.h"
#include "RateLimiter.h"
#include "StreamForwarder.h"
#include "StreamManager.h"
#include "StreamManagerMock.h"
#include <condition_variable>
#include <gmock/gmock.h>
#include <mutex>
#endif

namespace Aws
{
namespace IoTFleetWise
{

class CollectionInspectionWorkerThreadTest : public ::testing::Test
{
protected:
    std::shared_ptr<InspectionMatrix> collectionSchemes;
    std::shared_ptr<const InspectionMatrix> consCollectionSchemes;
    std::vector<std::shared_ptr<ExpressionNode>> expressionNodes;
    SignalBufferPtr signalBuffer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<DataSenderQueue> outputCollectedData;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    std::shared_ptr<Aws::IoTFleetWise::Store::StreamForwarder> streamForwarder;
    std::shared_ptr<::testing::StrictMock<Testing::StreamManagerMock>> streamManager;
    std::shared_ptr<RateLimiter> rateLimiter;
#endif

    void
    initAndStartWorker( CollectionInspectionWorkerThread &worker )
    {
        bool res = worker.init( signalBuffer,
                                outputCollectedData,
                                1000,
                                nullptr
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                ,
                                streamForwarder,
                                streamManager
#endif
        );
        ASSERT_TRUE( res );
        ASSERT_TRUE( worker.start() );
    }

    std::shared_ptr<ExpressionNode>
    getAlwaysTrueCondition()
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        expressionNodes.back()->nodeType = ExpressionNodeType::BOOLEAN;
        expressionNodes.back()->booleanValue = true;
        return ( expressionNodes.back() );
    }

    std::shared_ptr<ExpressionNode>
    getSignalsBiggerCondition( SignalID id1, double threshold1 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto bigger1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value1 = expressionNodes.back();

        bigger1->nodeType = ExpressionNodeType::OPERATOR_BIGGER;
        bigger1->left = signal1.get();
        bigger1->right = value1.get();

        signal1->nodeType = ExpressionNodeType::SIGNAL;
        signal1->signalID = id1;

        value1->nodeType = ExpressionNodeType::FLOAT;
        value1->floatingValue = threshold1;

        return bigger1;
    }

    std::shared_ptr<ExpressionNode>
    getAlwaysFalseCondition()
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        expressionNodes.back()->nodeType = ExpressionNodeType::BOOLEAN;
        expressionNodes.back()->booleanValue = false;
        return ( expressionNodes.back() );
    }

    template <typename T>
    bool
    popCollectedData( std::shared_ptr<const T> &collectedData )
    {
        std::shared_ptr<const DataToSend> senderData;
        auto succeeded = outputCollectedData->pop( senderData );
        collectedData = std::dynamic_pointer_cast<const T>( senderData );
        return succeeded;
    }

    void
    SetUp() override
    {
        collectionSchemes = std::make_shared<InspectionMatrix>();
        consCollectionSchemes = std::shared_ptr<const InspectionMatrix>( collectionSchemes );
        collectionSchemes->conditions.resize( 4 );
        collectionSchemes->conditions[0].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[1].condition = getAlwaysFalseCondition().get();

        signalBuffer.reset( new SignalBuffer( 1000, "Signal Buffer" ) );
        // Init the output buffer
        outputCollectedData = std::make_shared<DataSenderQueue>( 3, "Collected Data" );

#ifdef FWE_FEATURE_STORE_AND_FORWARD
        CANInterfaceIDTranslator canIDTranslator;
        auto protoWriter = std::make_shared<DataSenderProtoWriter>( canIDTranslator, nullptr );
        streamManager = std::make_shared<::testing::StrictMock<Testing::StreamManagerMock>>( protoWriter );
        // by default, forward data to DataSenderQueue
        EXPECT_CALL( *streamManager, appendToStreams( ::testing::_ ) )
            .Times( ::testing::AnyNumber() )
            .WillRepeatedly( ::testing::Return( Store::StreamManager::ReturnCode::STREAM_NOT_FOUND ) );

        rateLimiter = std::make_shared<RateLimiter>();
        streamForwarder =
            std::make_shared<Aws::IoTFleetWise::Store::StreamForwarder>( streamManager, nullptr, rateLimiter );
#endif
    }

    void
    TearDown() override
    {
    }
};

TEST_F( CollectionInspectionWorkerThreadTest, CollectBurstWithoutSubsampling )
{
    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread worker( engine );
    initAndStartWorker( worker );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    s1.isConditionOnlySignal = false;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2222;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 77777;
    s2.signalType = SignalType::INT32;
    s2.isConditionOnlySignal = false;
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 3333;
    s3.sampleBufferSize = 50;
    s3.minimumSampleIntervalMs = 0;
    s3.fixedWindowPeriod = 77777;
    s3.signalType = SignalType::BOOLEAN;
    s3.isConditionOnlySignal = false;
    InspectionMatrixCanFrameCollectionInfo c1;
    c1.frameID = 0x380;
    c1.channelID = 3;
    c1.sampleBufferSize = 10;
    c1.minimumSampleIntervalMs = 0;
    collectionSchemes->conditions[0].canFrames.push_back( c1 );
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].signals.push_back( s2 );
    collectionSchemes->conditions[0].signals.push_back( s3 );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( s1.signalID, 1 ).get();
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp, 0.1, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( s3.signalID, timestamp, 0, s3.signalType ) );
    collectedSignalsGroup.push_back( CollectedSignal( s2.signalID, timestamp, 10, s2.signalType ) );

    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };

    signalBuffer->push( CollectedDataFrame(
        collectedSignalsGroup,
        std::make_shared<CollectedCanRawFrame>( c1.frameID, c1.channelID, timestamp, buf1, sizeof( buf1 ) ) ) );

    collectedSignalsGroup.clear();

    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp + 1, 0.2, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( s3.signalID, timestamp + 1, 1, s3.signalType ) );
    collectedSignalsGroup.push_back( CollectedSignal( s2.signalID, timestamp + 1, 15, s2.signalType ) );

    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf2 = { 0xBA, 0xAD, 0xAF, 0xFE, 0x0, 0x0, 0x0, 0x0 };
    signalBuffer->push( CollectedDataFrame(
        collectedSignalsGroup,
        std::make_shared<CollectedCanRawFrame>( c1.frameID, c1.channelID, timestamp, buf2, sizeof( buf2 ) ) ) );

    collectedSignalsGroup.clear();

    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp + 2, 1.5, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( s2.signalID, timestamp + 2, 20, s2.signalType ) );
    collectedSignalsGroup.push_back( CollectedSignal( s3.signalID, timestamp + 2, 0, s3.signalType ) );

    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf3 = { 0xCA, 0xFE, 0xF0, 0x0D, 0x0, 0x0, 0x0, 0x0 };
    signalBuffer->push( CollectedDataFrame(
        collectedSignalsGroup,
        std::make_shared<CollectedCanRawFrame>( c1.frameID, c1.channelID, timestamp, buf3, sizeof( buf3 ) ) ) );

    worker.onNewDataAvailable();

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    ASSERT_EQ( collectedData->signals.size(), 9 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 1.5 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.2 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );

    EXPECT_EQ( collectedData->signals[3].value.value.int32Val, 20 );
    EXPECT_EQ( collectedData->signals[4].value.value.int32Val, 15 );
    EXPECT_EQ( collectedData->signals[5].value.value.int32Val, 10 );

    EXPECT_EQ( collectedData->signals[6].value.value.boolVal, 0 );
    EXPECT_EQ( collectedData->signals[7].value.value.boolVal, 1 );
    EXPECT_EQ( collectedData->signals[8].value.value.boolVal, 0 );

    ASSERT_EQ( collectedData->canFrames.size(), 3 );

    EXPECT_EQ( collectedData->canFrames[0].data, buf3 );
    EXPECT_EQ( collectedData->canFrames[1].data, buf2 );
    EXPECT_EQ( collectedData->canFrames[2].data, buf1 );

    ASSERT_FALSE( popCollectedData( collectedData ) );

    // Check changing the inspection matrix when already running:
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    worker.stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, CollectionQueueFull )
{
    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread worker( engine );
    initAndStartWorker( worker );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = false;
    s1.signalType = SignalType::DOUBLE;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    collectionSchemes->conditions[1].signals.push_back( s1 );
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();
    collectionSchemes->conditions[2].signals.push_back( s1 );
    collectionSchemes->conditions[2].condition = getAlwaysTrueCondition().get();
    collectionSchemes->conditions[3].signals.push_back( s1 );
    collectionSchemes->conditions[3].condition = getAlwaysTrueCondition().get();
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp, 1, SignalType::DOUBLE ) );
    signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) );
    worker.onNewDataAvailable();

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_FALSE( popCollectedData( collectedData ) );

    worker.stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, ConsumeDataWithoutNotify )
{
    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread worker( engine );
    initAndStartWorker( worker );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = false;
    s1.signalType = SignalType::DOUBLE;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( s1.signalID, 1 ).get();
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp, 0.1, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );
    collectedSignalsGroup.clear();

    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp + 2, 0.2, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );
    collectedSignalsGroup.clear();

    collectedSignalsGroup.push_back( CollectedSignal( s1.signalID, timestamp + 3, 1.5, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );
    collectedSignalsGroup.clear();

    // After one second even without notifying data in the queue should be consumed
    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    ASSERT_EQ( collectedData->signals.size(), 3U );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 1.5 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.2 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );

    DELAY_ASSERT_FALSE( popCollectedData( collectedData ) );

    worker.stop();
}
/**
 * @brief This test cases validates that if a collectionScheme has the collection of DTCs enabled,
 * then the expected output should include them.
 */
TEST_F( CollectionInspectionWorkerThreadTest, ConsumeActiveDTCsCollectionSchemeHasEnabledDTCs )
{
    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread inspectionWorker( engine );
    initAndStartWorker( inspectionWorker );
    // Test case 1 : Create a set of DTCs and make sure they are available in the collected data
    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    dtcInfo.mDTCCodes.emplace_back( "C0196" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = mClock->systemTimeSinceEpochMs();
    ASSERT_TRUE( dtcInfo.hasItems() );
    // Push the DTCs to the buffer
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( std::make_shared<DTCInfo>( dtcInfo ) ) ) );
    // Prepare a condition to evaluate and expect the DTCs to be collected.
    InspectionMatrixSignalCollectionInfo signal{};
    signal.signalID = 1234;
    signal.sampleBufferSize = 50;
    signal.minimumSampleIntervalMs = 0;
    signal.fixedWindowPeriod = 77777;
    signal.isConditionOnlySignal = false;
    signal.signalType = SignalType::DOUBLE;
    collectionSchemes->conditions[0].signals.push_back( signal );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( signal.signalID, 1 ).get();
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    // Make sure that DTCs should be collected
    collectionSchemes->conditions[0].includeActiveDtcs = true;
    inspectionWorker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 0.1, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp + 1, 0.2, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp + 2, 1.5, SignalType::DOUBLE ) );
    // Push the signals so that the condition is met
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;

    // Expect the data to be collected and has the DTCs
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( collectedData->mDTCInfo.hasItems() );
    ASSERT_EQ( collectedData->mDTCInfo.mSID, SID::STORED_DTC );
    ASSERT_EQ( collectedData->mDTCInfo.mDTCCodes[0], "P0143" );
    ASSERT_EQ( collectedData->mDTCInfo.mDTCCodes[1], "C0196" );
    // Test case 2 : Change the DTCs and expect the new values are reflected in the new collection
    // Cycle
    dtcInfo.mDTCCodes.clear();
    dtcInfo.mDTCCodes.emplace_back( "B0148" );
    dtcInfo.mDTCCodes.emplace_back( "U0148" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = mClock->systemTimeSinceEpochMs();
    // Push the DTCs to the buffer
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( std::make_shared<DTCInfo>( dtcInfo ) ) ) );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( std::make_shared<DTCInfo>( dtcInfo ) ) ) );
    timestamp = mClock->systemTimeSinceEpochMs();
    // Push the signals so that the condition is met

    CollectedSignalsGroup collectedSignalGroup;
    collectedSignalGroup.push_back( CollectedSignal( signal.signalID, timestamp, 0.1, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( collectedSignalGroup ) );
    collectedSignalGroup.clear();

    collectedSignalGroup.push_back( CollectedSignal( signal.signalID, timestamp + 1, 0.2, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( collectedSignalGroup ) );
    collectedSignalGroup.clear();

    collectedSignalGroup.push_back( CollectedSignal( signal.signalID, timestamp + 2, 1.5, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( collectedSignalGroup ) );

    // Expect the data to be collected and has the DTCs
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( collectedData->mDTCInfo.hasItems() );
    ASSERT_EQ( collectedData->mDTCInfo.mSID, SID::STORED_DTC );
    ASSERT_EQ( collectedData->mDTCInfo.mDTCCodes[0], "B0148" );
    ASSERT_EQ( collectedData->mDTCInfo.mDTCCodes[1], "U0148" );

    inspectionWorker.stop();
}

// This test cases validates that if a collectionScheme has the collection of DTCs disabled,
// then the expected output should NOT include them
TEST_F( CollectionInspectionWorkerThreadTest, ConsumeActiveDTCsCollectionSchemeHasDisabledDTCs )
{
    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread inspectionWorker( engine );
    initAndStartWorker( inspectionWorker );
    // Create a set of DTCs and make sure they are NOT available in the collected data
    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    dtcInfo.mDTCCodes.emplace_back( "C0196" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = mClock->systemTimeSinceEpochMs();
    ASSERT_TRUE( dtcInfo.hasItems() );
    // Push the DTCs to the buffer
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( std::make_shared<DTCInfo>( dtcInfo ) ) ) );
    // Prepare a condition to evaluate and expect the DTCs to be NOT collected.
    InspectionMatrixSignalCollectionInfo signal{};
    signal.signalID = 1234;
    signal.sampleBufferSize = 50;
    signal.minimumSampleIntervalMs = 0;
    signal.fixedWindowPeriod = 77777;
    signal.isConditionOnlySignal = false;
    signal.signalType = SignalType::DOUBLE;
    collectionSchemes->conditions[0].signals.push_back( signal );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( signal.signalID, 1 ).get();
    // Make sure that DTCs should NOT be collected
    collectionSchemes->conditions[0].includeActiveDtcs = false;
    inspectionWorker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    // Push the signals so that the condition is met
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 0.1, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 0.2, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 1.5, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    std::shared_ptr<const DataToSend> senderData;

    // Expect the data to be collected and has NO DTCs
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_FALSE( collectedData->mDTCInfo.hasItems() );

    inspectionWorker.stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, StartWithoutInit )
{
    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread worker( engine );
    worker.onChangeInspectionMatrix( consCollectionSchemes );
}

#ifdef FWE_FEATURE_STORE_AND_FORWARD
TEST_F( CollectionInspectionWorkerThreadTest, PutStoreAndForwardDataIntoStream )
{
    std::mutex mutex;
    std::condition_variable appendComplete;
    bool done = false;
    EXPECT_CALL( *streamManager, appendToStreams( ::testing::_ ) )
        .WillOnce( ::testing::Invoke( [&]( const TriggeredCollectionSchemeData & ) -> Store::StreamManager::ReturnCode {
            std::lock_guard<std::mutex> lock( mutex );
            done = true;
            appendComplete.notify_one();
            return Store::StreamManager::ReturnCode::SUCCESS;
        } ) );

    CollectionInspectionEngine engine;
    CollectionInspectionWorkerThread inspectionWorker( engine );
    initAndStartWorker( inspectionWorker );

    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    dtcInfo.mDTCCodes.emplace_back( "C0196" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = mClock->systemTimeSinceEpochMs();
    ASSERT_TRUE( dtcInfo.hasItems() );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( std::make_shared<DTCInfo>( dtcInfo ) ) ) );
    InspectionMatrixSignalCollectionInfo signal{};
    signal.signalID = 1234;
    signal.sampleBufferSize = 50;
    signal.minimumSampleIntervalMs = 0;
    signal.fixedWindowPeriod = 77777;
    signal.isConditionOnlySignal = false;
    signal.signalType = SignalType::DOUBLE;
    collectionSchemes->conditions[0].signals.push_back( signal );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( signal.signalID, 1 ).get();
    collectionSchemes->conditions[0].includeActiveDtcs = false;
    inspectionWorker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    // Push the signals so that the condition is met
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 0.1, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 0.2, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( signal.signalID, timestamp, 1.5, SignalType::DOUBLE ) );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );

    // verify data was appended to stream
    {
        std::unique_lock<std::mutex> lock( mutex );
        EXPECT_TRUE( appendComplete.wait_for( lock, std::chrono::seconds( 2 ), [&done] {
            return done;
        } ) );
    }

    // verify data was not forwarded to the queue
    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    ASSERT_FALSE( popCollectedData( collectedData ) );

    inspectionWorker.stop();
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
