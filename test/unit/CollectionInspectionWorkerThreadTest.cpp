// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionInspectionWorkerThread.h"
#include "CANDataTypes.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "ICollectionScheme.h"
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "WaitUntil.h"
#include <array>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <cstdint>
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
    std::shared_ptr<InspectionMatrix> collectionSchemes;
    std::shared_ptr<const InspectionMatrix> consCollectionSchemes;
    std::vector<std::shared_ptr<ExpressionNode>> expressionNodes;
    SignalBufferPtr signalBufferPtr;
    CANBufferPtr canRawBufferPtr;
    ActiveDTCBufferPtr activeDTCBufferPtr;
    std::shared_ptr<const Clock> fClock = ClockHandler::getClock();
    std::shared_ptr<CollectedDataReadyToPublish> outputCollectedData;

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

    void
    SetUp() override
    {
        collectionSchemes = std::make_shared<InspectionMatrix>();
        consCollectionSchemes = std::shared_ptr<const InspectionMatrix>( collectionSchemes );
        collectionSchemes->conditions.resize( 4 );
        collectionSchemes->conditions[0].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[0].probabilityToSend = 1.0;
        collectionSchemes->conditions[1].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[1].probabilityToSend = 1.0;

        signalBufferPtr.reset( new SignalBuffer( 1000 ) );
        // CAN Buffer are a lock-free multi-producer single consumer buffer
        canRawBufferPtr.reset( new CANBuffer( 1000 ) );
        // DTC Buffer are a single producer single consumer buffer
        activeDTCBufferPtr.reset( new ActiveDTCBuffer( 1000 ) );
        // Init the output buffer
        outputCollectedData = std::make_shared<CollectedDataReadyToPublish>( 3 );
    }

    void
    TearDown() override
    {
    }
};

TEST_F( CollectionInspectionWorkerThreadTest, CollectBurstWithoutSubsampling )
{
    CollectionInspectionWorkerThread worker;
    ASSERT_TRUE( worker.init( signalBufferPtr, canRawBufferPtr, activeDTCBufferPtr, outputCollectedData, 1000 ) );
    ASSERT_TRUE( worker.start() );
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
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( s1.signalID, 1 ).get();
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = fClock->systemTimeSinceEpochMs();
    signalBufferPtr->push( CollectedSignal( s3.signalID, timestamp, 0, s3.signalType ) );
    signalBufferPtr->push( CollectedSignal( s3.signalID, timestamp, 1, s3.signalType ) );
    signalBufferPtr->push( CollectedSignal( s3.signalID, timestamp, 0, s3.signalType ) );
    signalBufferPtr->push( CollectedSignal( s2.signalID, timestamp, 10, s2.signalType ) );
    signalBufferPtr->push( CollectedSignal( s2.signalID, timestamp, 15, s2.signalType ) );
    signalBufferPtr->push( CollectedSignal( s2.signalID, timestamp, 20, s2.signalType ) );
    signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 0.1 ) );
    signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 0.2 ) );
    signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 1.5 ) );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf1 = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    canRawBufferPtr->push( CollectedCanRawFrame( c1.frameID, c1.channelID, timestamp, buf1, sizeof( buf1 ) ) );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf2 = { 0xBA, 0xAD, 0xAF, 0xFE, 0x0, 0x0, 0x0, 0x0 };
    canRawBufferPtr->push( CollectedCanRawFrame( c1.frameID, c1.channelID, timestamp, buf2, sizeof( buf2 ) ) );
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf3 = { 0xCA, 0xFE, 0xF0, 0x0D, 0x0, 0x0, 0x0, 0x0 };
    canRawBufferPtr->push( CollectedCanRawFrame( c1.frameID, c1.channelID, timestamp, buf3, sizeof( buf3 ) ) );

    worker.onNewDataAvailable();

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    WAIT_ASSERT_TRUE( outputCollectedData->pop( collectedData ) );

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

    ASSERT_FALSE( outputCollectedData->pop( collectedData ) );

    // Check changing the inspection matrix when already running:
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    worker.stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, CollectionQueueFull )
{
    CollectionInspectionWorkerThread worker;
    ASSERT_TRUE( worker.init( signalBufferPtr, canRawBufferPtr, activeDTCBufferPtr, outputCollectedData, 1000 ) );
    ASSERT_TRUE( worker.start() );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = false;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    collectionSchemes->conditions[1].signals.push_back( s1 );
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();
    collectionSchemes->conditions[2].signals.push_back( s1 );
    collectionSchemes->conditions[2].condition = getAlwaysTrueCondition().get();
    collectionSchemes->conditions[3].signals.push_back( s1 );
    collectionSchemes->conditions[3].condition = getAlwaysTrueCondition().get();
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = fClock->systemTimeSinceEpochMs();
    signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 1 ) );
    worker.onNewDataAvailable();

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;
    WAIT_ASSERT_TRUE( outputCollectedData->pop( collectedData ) );
    ASSERT_TRUE( outputCollectedData->pop( collectedData ) );
    ASSERT_TRUE( outputCollectedData->pop( collectedData ) );
    ASSERT_FALSE( outputCollectedData->pop( collectedData ) );

    worker.stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, ConsumeDataWithoutNotify )
{
    CollectionInspectionWorkerThread worker;
    ASSERT_TRUE( worker.init( signalBufferPtr, canRawBufferPtr, activeDTCBufferPtr, outputCollectedData, 1000 ) );
    ASSERT_TRUE( worker.start() );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = false;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( s1.signalID, 1 ).get();
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    worker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = fClock->systemTimeSinceEpochMs();

    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 0.1 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 0.2 ) ) );
    // // this fulfills condition >1 so should trigger
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( s1.signalID, timestamp, 1.5 ) ) );

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;

    // After one second even without notifying data in the queue should be consumed
    WAIT_ASSERT_TRUE( outputCollectedData->pop( collectedData ) );

    ASSERT_EQ( collectedData->signals.size(), 3U );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 1.5 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.2 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );

    DELAY_ASSERT_FALSE( outputCollectedData->pop( collectedData ) );

    worker.stop();
}
/**
 * @brief This test cases validates that if a collectionScheme has the collection of DTCs enabled,
 * then the expected output should include them.
 */
TEST_F( CollectionInspectionWorkerThreadTest, ConsumeActiveDTCsCollectionSchemeHasEnabledDTCs )
{
    CollectionInspectionWorkerThread inspectionWorker;
    ASSERT_TRUE(
        inspectionWorker.init( signalBufferPtr, canRawBufferPtr, activeDTCBufferPtr, outputCollectedData, 1000 ) );
    ASSERT_TRUE( inspectionWorker.start() );
    // Test case 1 : Create a set of DTCs and make sure they are available in the collected data
    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    dtcInfo.mDTCCodes.emplace_back( "C0196" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = fClock->systemTimeSinceEpochMs();
    ASSERT_TRUE( dtcInfo.hasItems() );
    // Push the DTCs to the buffer
    ASSERT_TRUE( activeDTCBufferPtr->push( dtcInfo ) );
    // Prepare a condition to evaluate and expect the DTCs to be collected.
    InspectionMatrixSignalCollectionInfo signal{};
    signal.signalID = 1234;
    signal.sampleBufferSize = 50;
    signal.minimumSampleIntervalMs = 0;
    signal.fixedWindowPeriod = 77777;
    signal.isConditionOnlySignal = false;
    collectionSchemes->conditions[0].signals.push_back( signal );
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( signal.signalID, 1 ).get();
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    // Make sure that DTCs should be collected
    collectionSchemes->conditions[0].includeActiveDtcs = true;
    inspectionWorker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = fClock->systemTimeSinceEpochMs();

    // Push the signals so that the condition is met
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 0.1 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 0.2 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 1.5 ) ) );

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;

    // Expect the data to be collected and has the DTCs
    WAIT_ASSERT_TRUE( outputCollectedData->pop( collectedData ) );
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
    dtcInfo.receiveTime = fClock->systemTimeSinceEpochMs();
    // Push the DTCs to the buffer
    ASSERT_TRUE( activeDTCBufferPtr->push( dtcInfo ) );
    timestamp = fClock->systemTimeSinceEpochMs();
    // Push the signals so that the condition is met

    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 0.1 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 0.2 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 1.5 ) ) );

    // Expect the data to be collected and has the DTCs
    WAIT_ASSERT_TRUE( outputCollectedData->pop( collectedData ) );
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
    CollectionInspectionWorkerThread inspectionWorker;
    ASSERT_TRUE(
        inspectionWorker.init( signalBufferPtr, canRawBufferPtr, activeDTCBufferPtr, outputCollectedData, 1000 ) );
    ASSERT_TRUE( inspectionWorker.start() );
    // Create a set of DTCs and make sure they are NOT available in the collected data
    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.emplace_back( "P0143" );
    dtcInfo.mDTCCodes.emplace_back( "C0196" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = fClock->systemTimeSinceEpochMs();
    ASSERT_TRUE( dtcInfo.hasItems() );
    // Push the DTCs to the buffer
    ASSERT_TRUE( activeDTCBufferPtr->push( dtcInfo ) );
    // Prepare a condition to evaluate and expect the DTCs to be NOT collected.
    InspectionMatrixSignalCollectionInfo signal{};
    signal.signalID = 1234;
    signal.sampleBufferSize = 50;
    signal.minimumSampleIntervalMs = 0;
    signal.fixedWindowPeriod = 77777;
    signal.isConditionOnlySignal = false;
    collectionSchemes->conditions[0].signals.push_back( signal );
    collectionSchemes->conditions[0].condition = getSignalsBiggerCondition( signal.signalID, 1 ).get();
    // Make sure that DTCs should NOT be collected
    collectionSchemes->conditions[0].includeActiveDtcs = false;
    inspectionWorker.onChangeInspectionMatrix( consCollectionSchemes );
    Timestamp timestamp = fClock->systemTimeSinceEpochMs();

    // Push the signals so that the condition is met
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 0.1 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 0.2 ) ) );
    ASSERT_TRUE( signalBufferPtr->push( CollectedSignal( signal.signalID, timestamp, 1.5 ) ) );

    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData;

    // Expect the data to be collected and has NO DTCs
    WAIT_ASSERT_TRUE( outputCollectedData->pop( collectedData ) );
    ASSERT_FALSE( collectedData->mDTCInfo.hasItems() );

    inspectionWorker.stop();
}

TEST_F( CollectionInspectionWorkerThreadTest, StartWithoutInit )
{
    CollectionInspectionWorkerThread worker;
    worker.onChangeInspectionMatrix( consCollectionSchemes );
}

} // namespace IoTFleetWise
} // namespace Aws
