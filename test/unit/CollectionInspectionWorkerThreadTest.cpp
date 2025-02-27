// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CollectionInspectionWorkerThread.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CollectionInspectionEngine.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class CollectionInspectionWorkerThreadTest : public ::testing::Test
{
protected:
    std::unique_ptr<CollectionInspectionEngine> engine;
    std::unique_ptr<CollectionInspectionWorkerThread> worker;
    std::shared_ptr<InspectionMatrix> collectionSchemes;
    std::shared_ptr<const InspectionMatrix> consCollectionSchemes;
    std::vector<std::shared_ptr<ExpressionNode>> expressionNodes;
    SignalBufferPtr signalBuffer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<DataSenderQueue> outputCollectedData;

    CollectionInspectionWorkerThreadTest()
    {
    }

    void
    SetUp() override
    {
        collectionSchemes = std::make_shared<InspectionMatrix>();
        consCollectionSchemes = std::shared_ptr<const InspectionMatrix>( collectionSchemes );
        collectionSchemes->conditions.resize( 4 );
        collectionSchemes->conditions[0].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[1].condition = getAlwaysFalseCondition().get();

        signalBuffer = std::make_shared<SignalBuffer>( 1000, "Signal Buffer" );
        // Init the output buffer
        outputCollectedData = std::make_shared<DataSenderQueue>( 3, "Collected Data" );

        engine = std::make_unique<CollectionInspectionEngine>( nullptr );
        worker = std::make_unique<CollectionInspectionWorkerThread>(
            *engine, signalBuffer, outputCollectedData, 1000, nullptr );
    }

    void
    TearDown() override
    {
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
};

TEST_F( CollectionInspectionWorkerThreadTest, CollectBurstWithoutSubsampling )
{
    ASSERT_TRUE( worker->start() );
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
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].signals.push_back( s2 );
    collectionSchemes->conditions[0].signals.push_back( s3 );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( s1.signalID, 1 ).get();
    worker->onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.emplace_back( s1.signalID, timestamp, 0.1, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( s3.signalID, timestamp, 0, s3.signalType );
    collectedSignalsGroup.emplace_back( s2.signalID, timestamp, 10, s2.signalType );

    signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) );

    collectedSignalsGroup.clear();

    collectedSignalsGroup.emplace_back( s1.signalID, timestamp + 1, 0.2, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( s3.signalID, timestamp + 1, 1, s3.signalType );
    collectedSignalsGroup.emplace_back( s2.signalID, timestamp + 1, 15, s2.signalType );

    signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) );

    collectedSignalsGroup.clear();

    collectedSignalsGroup.emplace_back( s1.signalID, timestamp + 2, 1.5, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( s2.signalID, timestamp + 2, 20, s2.signalType );
    collectedSignalsGroup.emplace_back( s3.signalID, timestamp + 2, 0, s3.signalType );

    signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) );

    worker->onNewDataAvailable();

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

    ASSERT_FALSE( popCollectedData( collectedData ) );

    // Check changing the inspection matrix when already running:
    worker->onChangeInspectionMatrix( consCollectionSchemes );
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    worker->stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, CollectionQueueFull )
{
    ASSERT_TRUE( worker->start() );
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
    worker->onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.emplace_back( s1.signalID, timestamp, 1, SignalType::DOUBLE );
    signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) );
    worker->onNewDataAvailable();

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_FALSE( popCollectedData( collectedData ) );

    worker->stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, ConsumeDataWithoutNotify )
{
    ASSERT_TRUE( worker->start() );
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
    worker->onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.emplace_back( s1.signalID, timestamp, 0.1, SignalType::DOUBLE );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );
    collectedSignalsGroup.clear();

    collectedSignalsGroup.emplace_back( s1.signalID, timestamp + 2, 0.2, SignalType::DOUBLE );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );
    collectedSignalsGroup.clear();

    collectedSignalsGroup.emplace_back( s1.signalID, timestamp + 3, 1.5, SignalType::DOUBLE );
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

    worker->stop();
}
/**
 * @brief This test cases validates that if a collectionScheme has the collection of DTCs enabled,
 * then the expected output should include them.
 */
TEST_F( CollectionInspectionWorkerThreadTest, ConsumeActiveDTCsCollectionSchemeHasEnabledDTCs )
{
    ASSERT_TRUE( worker->start() );
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
    worker->onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.emplace_back( signal.signalID, timestamp, 0.1, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( signal.signalID, timestamp + 1, 0.2, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( signal.signalID, timestamp + 2, 1.5, SignalType::DOUBLE );
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
    collectedSignalGroup.emplace_back( signal.signalID, timestamp, 0.1, SignalType::DOUBLE );
    ASSERT_TRUE( signalBuffer->push( collectedSignalGroup ) );
    collectedSignalGroup.clear();

    collectedSignalGroup.emplace_back( signal.signalID, timestamp + 1, 0.2, SignalType::DOUBLE );
    ASSERT_TRUE( signalBuffer->push( collectedSignalGroup ) );
    collectedSignalGroup.clear();

    collectedSignalGroup.emplace_back( signal.signalID, timestamp + 2, 1.5, SignalType::DOUBLE );
    ASSERT_TRUE( signalBuffer->push( collectedSignalGroup ) );

    // Expect the data to be collected and has the DTCs
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_TRUE( collectedData->mDTCInfo.hasItems() );
    ASSERT_EQ( collectedData->mDTCInfo.mSID, SID::STORED_DTC );
    ASSERT_EQ( collectedData->mDTCInfo.mDTCCodes[0], "B0148" );
    ASSERT_EQ( collectedData->mDTCInfo.mDTCCodes[1], "U0148" );

    worker->stop();
}

// This test cases validates that if a collectionScheme has the collection of DTCs disabled,
// then the expected output should NOT include them
TEST_F( CollectionInspectionWorkerThreadTest, ConsumeActiveDTCsCollectionSchemeHasDisabledDTCs )
{
    ASSERT_TRUE( worker->start() );
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
    worker->onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    // Push the signals so that the condition is met
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.emplace_back( signal.signalID, timestamp, 0.1, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( signal.signalID, timestamp, 0.2, SignalType::DOUBLE );
    collectedSignalsGroup.emplace_back( signal.signalID, timestamp, 1.5, SignalType::DOUBLE );
    ASSERT_TRUE( signalBuffer->push( CollectedDataFrame( collectedSignalsGroup ) ) );

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    std::shared_ptr<const DataToSend> senderData;

    // Expect the data to be collected and has NO DTCs
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    ASSERT_FALSE( collectedData->mDTCInfo.hasItems() );

    worker->stop();
}
} // namespace IoTFleetWise
} // namespace Aws
