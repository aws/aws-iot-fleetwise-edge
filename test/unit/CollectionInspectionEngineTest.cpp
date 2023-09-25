// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionInspectionEngine.h"
#include "CANDataTypes.h"
#include "CollectionInspectionAPITypes.h"
#include "GeohashInfo.h"
#include "ICollectionScheme.h"
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "TimeTypes.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using signalTypes =
    ::testing::Types<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double>;

template <typename T>
class CollectionInspectionEngineTest : public ::testing::Test
{
protected:
    std::shared_ptr<InspectionMatrix> collectionSchemes;
    std::shared_ptr<const InspectionMatrix> consCollectionSchemes;
    std::vector<std::shared_ptr<ExpressionNode>> expressionNodes;

    bool
    compareSignalValue( const SignalValueWrapper &signalValueWrapper, T sigVal )
    {
        auto sigType = signalValueWrapper.getType();
        switch ( sigType )
        {
        case SignalType::UINT8:
            return static_cast<uint8_t>( sigVal ) == signalValueWrapper.value.uint8Val;
        case SignalType::INT8:
            return static_cast<int8_t>( sigVal ) == signalValueWrapper.value.int8Val;
        case SignalType::UINT16:
            return static_cast<uint16_t>( sigVal ) == signalValueWrapper.value.uint16Val;
        case SignalType::INT16:
            return static_cast<int16_t>( sigVal ) == signalValueWrapper.value.int16Val;
        case SignalType::UINT32:
            return static_cast<uint32_t>( sigVal ) == signalValueWrapper.value.uint32Val;
        case SignalType::INT32:
            return static_cast<int32_t>( sigVal ) == signalValueWrapper.value.int32Val;
        case SignalType::UINT64:
            return static_cast<uint64_t>( sigVal ) == signalValueWrapper.value.uint64Val;
        case SignalType::INT64:
            return static_cast<int64_t>( sigVal ) == signalValueWrapper.value.int64Val;
        case SignalType::FLOAT:
            return static_cast<float>( sigVal ) == signalValueWrapper.value.floatVal;
        case SignalType::DOUBLE:
            return static_cast<double>( sigVal ) == signalValueWrapper.value.doubleVal;
        case SignalType::BOOLEAN:
            return static_cast<bool>( sigVal ) == signalValueWrapper.value.boolVal;
        }
        return false;
    }

    void
    addSignalToCollect( ConditionWithCollectedData &collectionScheme, InspectionMatrixSignalCollectionInfo s )
    {
        collectionScheme.signals.push_back( s );
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
    getAlwaysFalseCondition()
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        expressionNodes.back()->nodeType = ExpressionNodeType::BOOLEAN;
        expressionNodes.back()->booleanValue = false;
        return ( expressionNodes.back() );
    }

    std::shared_ptr<ExpressionNode>
    getNotEqualCondition( SignalID id1, SignalID id2 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto notEqual = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal2 = expressionNodes.back();

        signal1->nodeType = ExpressionNodeType::SIGNAL;
        signal1->signalID = id1;

        signal2->nodeType = ExpressionNodeType::SIGNAL;
        signal2->signalID = id2;

        notEqual->nodeType = ExpressionNodeType::OPERATOR_NOT_EQUAL;
        notEqual->left = signal1.get();
        notEqual->right = signal2.get();
        return ( notEqual );
    }

    std::shared_ptr<ExpressionNode>
    getTwoSignalsBiggerCondition( SignalID id1, double threshold1, SignalID id2, double threshold2 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto boolAnd = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto bigger1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto bigger2 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal2 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value2 = expressionNodes.back();

        boolAnd->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_AND;
        boolAnd->left = bigger1.get();
        boolAnd->right = bigger2.get();

        bigger1->nodeType = ExpressionNodeType::OPERATOR_BIGGER;
        bigger1->left = signal1.get();
        bigger1->right = value1.get();

        bigger2->nodeType = ExpressionNodeType::OPERATOR_BIGGER;
        bigger2->left = signal2.get();
        bigger2->right = value2.get();

        signal1->nodeType = ExpressionNodeType::SIGNAL;
        signal1->signalID = id1;

        signal2->nodeType = ExpressionNodeType::SIGNAL;
        signal2->signalID = id2;

        value1->nodeType = ExpressionNodeType::FLOAT;
        value1->floatingValue = threshold1;

        value2->nodeType = ExpressionNodeType::FLOAT;
        value2->floatingValue = threshold2;

        return boolAnd;
    }

    std::shared_ptr<ExpressionNode>
    getGeohashFunctionCondition( SignalID latID, SignalID lonID, uint8_t precision )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto function = expressionNodes.back();
        function->nodeType = ExpressionNodeType::GEOHASHFUNCTION;
        function->function.geohashFunction.latitudeSignalID = latID;
        function->function.geohashFunction.longitudeSignalID = lonID;
        function->function.geohashFunction.precision = precision;
        return function;
    }

    std::shared_ptr<ExpressionNode>
    getMultiFixedWindowCondition( SignalID id1 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto boolOr = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto smaller1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto equal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto plus1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto minus1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto multiply1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto lastMin1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto previousLastMin1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto lastMax1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto previousLastMax1 = expressionNodes.back();

        boolOr->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_OR;
        boolOr->left = smaller1.get();
        boolOr->right = equal1.get();

        smaller1->nodeType = ExpressionNodeType::OPERATOR_SMALLER;
        smaller1->left = plus1.get();
        smaller1->right = multiply1.get();

        equal1->nodeType = ExpressionNodeType::OPERATOR_EQUAL;
        equal1->left = lastMin1.get();
        equal1->right = previousLastMin1.get();

        plus1->nodeType = ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS;
        plus1->left = minus1.get();
        plus1->right = previousLastMax1.get();

        minus1->nodeType = ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS;
        minus1->left = lastMax1.get();
        minus1->right = previousLastMax1.get();

        multiply1->nodeType = ExpressionNodeType::OPERATOR_ARITHMETIC_MULTIPLY;
        multiply1->left = lastMax1.get();
        multiply1->right = previousLastMax1.get();

        lastMin1->nodeType = ExpressionNodeType::WINDOWFUNCTION;
        lastMin1->function.windowFunction = WindowFunction::LAST_FIXED_WINDOW_MIN;
        lastMin1->signalID = id1;

        previousLastMin1->nodeType = ExpressionNodeType::WINDOWFUNCTION;
        previousLastMin1->function.windowFunction = WindowFunction::PREV_LAST_FIXED_WINDOW_MIN;
        previousLastMin1->signalID = id1;

        lastMax1->nodeType = ExpressionNodeType::WINDOWFUNCTION;
        lastMax1->function.windowFunction = WindowFunction::LAST_FIXED_WINDOW_MAX;
        lastMax1->signalID = id1;

        previousLastMax1->nodeType = ExpressionNodeType::WINDOWFUNCTION;
        previousLastMax1->function.windowFunction = WindowFunction::PREV_LAST_FIXED_WINDOW_MAX;
        previousLastMax1->signalID = id1;

        return boolOr;
    }

    std::shared_ptr<ExpressionNode>
    getTwoSignalsRatioCondition( SignalID id1, double threshold1, SignalID id2, double threshold2 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto not1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto not2 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto not3 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto boolAnd = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto smallerEqual1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto biggerEqual1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto divide1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal2 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value2 = expressionNodes.back();

        not1->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_NOT;
        not1->left = boolAnd.get();

        boolAnd->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_AND;
        boolAnd->left = not2.get();
        boolAnd->right = not3.get();

        not2->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_NOT;
        not2->left = smallerEqual1.get();

        not3->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_NOT;
        not3->left = biggerEqual1.get();

        value1->nodeType = ExpressionNodeType::FLOAT;
        value1->floatingValue = threshold1;

        value2->nodeType = ExpressionNodeType::FLOAT;
        value2->floatingValue = threshold2;

        signal1->nodeType = ExpressionNodeType::SIGNAL;
        signal1->signalID = id1;

        signal2->nodeType = ExpressionNodeType::SIGNAL;
        signal2->signalID = id2;

        smallerEqual1->nodeType = ExpressionNodeType::OPERATOR_SMALLER_EQUAL;
        smallerEqual1->left = signal1.get();
        smallerEqual1->right = value1.get();

        biggerEqual1->nodeType = ExpressionNodeType::OPERATOR_BIGGER_EQUAL;
        biggerEqual1->left = divide1.get();
        biggerEqual1->right = value2.get();

        divide1->nodeType = ExpressionNodeType::OPERATOR_ARITHMETIC_DIVIDE;
        divide1->left = signal1.get();
        divide1->right = signal2.get();

        return not1;
    }

    std::shared_ptr<ExpressionNode>
    getUnknownCondition( SignalID id1 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto smallerEqual1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto unknown1 = expressionNodes.back();

        signal1->nodeType = ExpressionNodeType::SIGNAL;
        signal1->signalID = id1;

        unknown1->nodeType = (ExpressionNodeType)-1;

        smallerEqual1->nodeType = ExpressionNodeType::OPERATOR_SMALLER_EQUAL;
        smallerEqual1->left = signal1.get();
        smallerEqual1->right = unknown1.get();

        return smallerEqual1;
    }

    std::shared_ptr<ExpressionNode>
    getLastAvgWindowBiggerCondition( SignalID id1, double threshold1 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto bigger1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto function1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value1 = expressionNodes.back();

        bigger1->nodeType = ExpressionNodeType::OPERATOR_BIGGER;
        bigger1->left = function1.get();
        bigger1->right = value1.get();

        function1->nodeType = ExpressionNodeType::WINDOWFUNCTION;
        function1->signalID = id1;
        function1->function.windowFunction = WindowFunction::LAST_FIXED_WINDOW_AVG;

        value1->nodeType = ExpressionNodeType::FLOAT;
        value1->floatingValue = threshold1;

        return bigger1;
    }

    std::shared_ptr<ExpressionNode>
    getPrevLastAvgWindowBiggerCondition( SignalID id1, double threshold1 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto bigger1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto function1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto value1 = expressionNodes.back();

        bigger1->nodeType = ExpressionNodeType::OPERATOR_BIGGER;
        bigger1->left = function1.get();
        bigger1->right = value1.get();

        function1->nodeType = ExpressionNodeType::WINDOWFUNCTION;
        function1->signalID = id1;
        function1->function.windowFunction = WindowFunction::PREV_LAST_FIXED_WINDOW_AVG;

        value1->nodeType = ExpressionNodeType::FLOAT;
        value1->floatingValue = threshold1;

        return bigger1;
    }

    void
    SetUp() override
    {
        collectionSchemes = std::make_shared<InspectionMatrix>();
        consCollectionSchemes = std::shared_ptr<const InspectionMatrix>( collectionSchemes );

        // Setup collectionSchemes->conditions. ;
        collectionSchemes->conditions.resize( 2 );

        collectionSchemes->conditions[0].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[0].probabilityToSend = 1.0;
        collectionSchemes->conditions[1].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[1].probabilityToSend = 1.0;
    }

    void
    TearDown() override
    {
    }
};

class CollectionInspectionEngineDoubleTest : public CollectionInspectionEngineTest<double>
{
};

TYPED_TEST_SUITE( CollectionInspectionEngineTest, signalTypes );

TYPED_TEST( CollectionInspectionEngineTest, TwoSignalsInConditionAndOneSignalToCollect )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 3;
    s3.sampleBufferSize = 50;
    s3.minimumSampleIntervalMs = 0;
    s3.fixedWindowPeriod = 77777;
    s3.isConditionOnlySignal = false;
    s3.signalType = getSignalType<TypeParam>();

    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s2 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s3 );

    // The condition is (signalID(1)>-100) && (signalID(2)>-500)
    this->collectionSchemes->conditions[0].condition =
        this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();
    engine.onChangeInspectionMatrix( this->consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    TypeParam testVal1 = 10;
    TypeParam testVal2 = 20;
    TypeParam testVal3 = 30;
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, testVal1 );
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, testVal2 );
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, testVal3 );

    // Signals for condition are not available yet so collectionScheme should not trigger
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    timestamp += 1000;
    engine.addNewSignal<double>( s1.signalID, timestamp, -90.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -1000.0 );

    // Condition only for first signal is fulfilled (-90 > -100) but second not so boolean and is false
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s2.signalID, timestamp, -480.0 );

    // Condition is fulfilled so it should trigger
    engine.evaluateConditions( timestamp );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );
    ASSERT_EQ( collectedData->signals[0].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[1].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[2].signalID, s3.signalID );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );
}

TEST_F( CollectionInspectionEngineDoubleTest, EndlessCondition )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    ExpressionNode endless;

    endless.nodeType = ExpressionNodeType::OPERATOR_LOGICAL_AND;
    endless.left = &endless;
    endless.right = &endless;

    collectionSchemes->conditions[0].condition = &endless;
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    EXPECT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TooBigForSignalBuffer )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 3072;
    s1.sampleBufferSize =
        500000000; // this number of samples should exceed the maximum buffer size defined in MAX_SAMPLE_MEMORY
    s1.minimumSampleIntervalMs = 5;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 ); // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, timestamp + 1000, 0.2 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 2000, 0.3 );

    engine.evaluateConditions( timestamp + 3000 );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 5000, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 0 );
}

TEST_F( CollectionInspectionEngineDoubleTest, TooBigForSignalBufferOverflow )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 536870912;
    s1.minimumSampleIntervalMs = 5;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    InspectionMatrixCanFrameCollectionInfo c1;
    c1.frameID = 0x380;
    c1.channelID = 3;
    c1.sampleBufferSize = 536870912;
    c1.minimumSampleIntervalMs = 0;
    collectionSchemes->conditions[0].canFrames.push_back( c1 );

    collectionSchemes->conditions[0].minimumPublishIntervalMs = 500;
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 1000, 0.2 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 2000, 0.3 );

    engine.evaluateConditions( timestamp + 3000 );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 4000, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 0 );
}

TEST_F( CollectionInspectionEngineDoubleTest, SignalBufferErasedAfterNewConditions )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 1, 0.2 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 2, 0.3 );

    // This call will flush the signal history buffer even when
    // exactly the same conditions are handed over.
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    engine.addNewSignal<double>( s1.signalID, timestamp + 3, 0.4 );

    engine.evaluateConditions( timestamp + 3 );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 3, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 0.4 );
}

TEST_F( CollectionInspectionEngineDoubleTest, CollectBurstWithoutSubsampling )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.2 );
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.3 );

    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 0.3 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.2 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );
}

TEST_F( CollectionInspectionEngineDoubleTest, IllegalSignalID )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 0xFFFFFFFF;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );
}

TEST_F( CollectionInspectionEngineDoubleTest, IllegalSampleSize )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 0; // Not allowed
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );
}

TEST_F( CollectionInspectionEngineDoubleTest, ZeroSignalsOnlyDTCCollection )
{
    CollectionInspectionEngine engine;
    collectionSchemes->conditions[0].includeActiveDtcs = true;
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.push_back( "B1217" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = timestamp.systemTimeMs;
    engine.setActiveDTCs( dtcInfo );
    timestamp += 1000;
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    EXPECT_EQ( collectedData->mDTCInfo.mDTCCodes[0], "B1217" );
}

TEST_F( CollectionInspectionEngineDoubleTest, CollectRawCanFrames )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixCanFrameCollectionInfo c1;
    c1.frameID = 0x380;
    c1.channelID = 3;
    c1.sampleBufferSize = 10;
    c1.minimumSampleIntervalMs = 0;
    collectionSchemes->conditions[0].canFrames.push_back( c1 );

    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp, buf, sizeof( buf ) );

    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->canFrames.size(), 1 );

    EXPECT_EQ( collectedData->canFrames[0].frameID, c1.frameID );
    EXPECT_EQ( collectedData->canFrames[0].channelId, c1.channelID );
    EXPECT_EQ( collectedData->canFrames[0].size, sizeof( buf ) );
    EXPECT_TRUE( 0 == std::memcmp( collectedData->canFrames[0].data.data(), buf.data(), sizeof( buf ) ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, CollectRawCanFDFrames )
{
    CollectionInspectionEngine engine;
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixCanFrameCollectionInfo c1;
    c1.frameID = 0x380;
    c1.channelID = 3;
    c1.sampleBufferSize = 10;
    c1.minimumSampleIntervalMs = 0;
    collectionSchemes->conditions[0].canFrames.push_back( c1 );

    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0, 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp, buf, sizeof( buf ) );

    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->canFrames.size(), 1 );

    EXPECT_EQ( collectedData->canFrames[0].frameID, c1.frameID );
    EXPECT_EQ( collectedData->canFrames[0].channelId, c1.channelID );
    EXPECT_EQ( collectedData->canFrames[0].size, sizeof( buf ) );
    EXPECT_TRUE( 0 == std::memcmp( collectedData->canFrames[0].data.data(), buf.data(), sizeof( buf ) ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultipleCanSubsampling )
{
    CollectionInspectionEngine engine;
    InspectionMatrixCanFrameCollectionInfo c1;
    c1.frameID = 0x380;
    c1.channelID = 3;
    c1.sampleBufferSize = 10;
    c1.minimumSampleIntervalMs = 100;
    collectionSchemes->conditions[0].canFrames.push_back( c1 );

    InspectionMatrixCanFrameCollectionInfo c2;
    c2.frameID = 0x380;
    c2.channelID = 3;
    c2.sampleBufferSize = 10;
    // Same frame id but different subsampling Interval
    c2.minimumSampleIntervalMs = 500;
    collectionSchemes->conditions[0].canFrames.push_back( c2 );

    // Add to a second collectionScheme. this should reuse the same buffers
    collectionSchemes->conditions[1].canFrames.push_back( c1 );
    collectionSchemes->conditions[1].canFrames.push_back( c2 );

    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf = { 0xDE, 0xAD, 0xBE, 0xEF, 0x0, 0x0, 0x0, 0x0 };
    engine.addNewRawCanFrame( c1.frameID,
                              c1.channelID,
                              timestamp,
                              buf,
                              sizeof( buf ) ); // this message in  goes with both subsampling
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp + 100, buf, sizeof( buf ) ); // 100 ms subsampling
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp + 200, buf, sizeof( buf ) ); // 100 ms subsampling
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp + 250, buf, sizeof( buf ) ); // ignored
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp + 300, buf, sizeof( buf ) ); // 100 ms subsampling
    engine.addNewRawCanFrame( c1.frameID, c1.channelID, timestamp + 400, buf, sizeof( buf ) ); // 100 ms subsampling
    engine.addNewRawCanFrame( c1.frameID,
                              c1.channelID,
                              timestamp + 500,
                              buf,
                              sizeof( buf ) ); // this message in  goes with both subsampling

    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->canFrames.size(), 8 );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultipleSubsamplingOfSameSignalUsedInConditions )
{
    CollectionInspectionEngine engine;

    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 5678;
    s3.sampleBufferSize = 150;
    s3.minimumSampleIntervalMs = 20;
    s3.fixedWindowPeriod = 300;
    addSignalToCollect( collectionSchemes->conditions[0], s3 );
    InspectionMatrixSignalCollectionInfo s5{};
    s5.signalID = 1111;
    s5.sampleBufferSize = 10;
    s5.minimumSampleIntervalMs = 20;
    s5.fixedWindowPeriod = 300;
    addSignalToCollect( collectionSchemes->conditions[0], s5 );
    collectionSchemes->conditions[0].condition =
        getTwoSignalsBiggerCondition( s1.signalID, 150.0, s3.signalID, 155.0 ).get();
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = s1.signalID;
    s2.sampleBufferSize = 200;
    s2.minimumSampleIntervalMs = 50; // Different subsampling then signal in other condition
    s2.fixedWindowPeriod = 150;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    InspectionMatrixSignalCollectionInfo s4{};
    s4.signalID = s3.signalID;
    s4.sampleBufferSize = 130;
    s4.minimumSampleIntervalMs = 60;
    s4.fixedWindowPeriod = 250;
    addSignalToCollect( collectionSchemes->conditions[1], s4 );
    InspectionMatrixSignalCollectionInfo s6{};
    s6.signalID = 2222;
    s6.sampleBufferSize = 180;
    s6.minimumSampleIntervalMs = 50;
    s6.fixedWindowPeriod = 250;
    addSignalToCollect( collectionSchemes->conditions[1], s6 );
    collectionSchemes->conditions[1].condition =
        getTwoSignalsBiggerCondition( s2.signalID, 165.0, s4.signalID, 170.0 ).get();
    collectionSchemes->conditions[1].minimumPublishIntervalMs = 10000;

    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };

    for ( int i = 0; i < 100; i++ )
    {
        engine.addNewSignal<double>( s1.signalID, timestamp + i * 10, i * 2 );
        engine.addNewSignal<double>( s3.signalID, timestamp + i * 10, i * 2 + 1 );
        engine.addNewSignal<double>( s5.signalID, timestamp + i * 10, 55555 );
        engine.addNewSignal<double>( s6.signalID, timestamp + i * 10, 77777 );
    }

    engine.evaluateConditions( timestamp );

    using CollectionType = std::shared_ptr<const TriggeredCollectionSchemeData>;
    uint32_t waitTimeMs = 0;
    std::vector<CollectionType> collectedDataList;
    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData =
        engine.collectNextDataToSend( timestamp + 100 * 10, waitTimeMs );
    while ( collectedData != nullptr )
    {
        collectedDataList.push_back( collectedData );
        collectedData = engine.collectNextDataToSend( timestamp + 100 * 10, waitTimeMs );
    }
    ASSERT_EQ( collectedDataList.size(), 2 );

    std::sort( collectedDataList.begin(), collectedDataList.end(), []( CollectionType a, CollectionType b ) {
        return a->signals.size() < b->signals.size();
    } );

    // condition[1] has higher subsampling so less signals so it should the first in sorted list
    EXPECT_GE( std::count_if( collectedDataList[0]->signals.begin(),
                              collectedDataList[0]->signals.end(),
                              []( CollectedSignal s ) {
                                  return s.signalID == 2222;
                              } ),
               1 );

    EXPECT_GE( std::count_if( collectedDataList[1]->signals.begin(),
                              collectedDataList[1]->signals.end(),
                              []( CollectedSignal s ) {
                                  return s.signalID == 1111;
                              } ),
               1 );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultipleFixedWindowsOfSameSignalUsedInConditions )
{
    CollectionInspectionEngine engine;

    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 500;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    InspectionMatrixSignalCollectionInfo s5{};
    s5.signalID = 1111;
    s5.sampleBufferSize = 500;
    s5.minimumSampleIntervalMs = 20;
    s5.fixedWindowPeriod = 0;
    addSignalToCollect( collectionSchemes->conditions[0], s5 );
    collectionSchemes->conditions[0].condition = getLastAvgWindowBiggerCondition( s1.signalID, 150.0 ).get();
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = s1.signalID;
    s2.sampleBufferSize = 500;
    s2.minimumSampleIntervalMs = 50; // Different subsampling then signal in other condition
    s2.fixedWindowPeriod = 200;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    InspectionMatrixSignalCollectionInfo s6{};
    s6.signalID = 2222;
    s6.sampleBufferSize = 500;
    s6.minimumSampleIntervalMs = 50;
    s6.fixedWindowPeriod = 250;
    addSignalToCollect( collectionSchemes->conditions[1], s6 );
    collectionSchemes->conditions[1].condition = getLastAvgWindowBiggerCondition( s2.signalID, 150.0 ).get();
    collectionSchemes->conditions[1].minimumPublishIntervalMs = 10000;

    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };

    for ( int i = 0; i < 300; i++ )
    {
        engine.addNewSignal<double>( s1.signalID, timestamp + i * 10, i * 2 );
        engine.addNewSignal<double>( s5.signalID, timestamp + i * 10, 55555 );
        engine.addNewSignal<double>( s6.signalID, timestamp + i * 10, 77777 );
    }

    engine.evaluateConditions( timestamp );

    using CollectionType = std::shared_ptr<const TriggeredCollectionSchemeData>;
    uint32_t waitTimeMs = 0;
    std::vector<CollectionType> collectedDataList;
    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData =
        engine.collectNextDataToSend( timestamp + 100 * 10, waitTimeMs );
    while ( collectedData != nullptr )
    {
        collectedDataList.push_back( collectedData );
        collectedData = engine.collectNextDataToSend( timestamp + 300 * 10, waitTimeMs );
    }

    ASSERT_EQ( collectedDataList.size(), 2 );

    std::sort( collectedDataList.begin(), collectedDataList.end(), []( CollectionType a, CollectionType b ) {
        return a->signals.size() < b->signals.size();
    } );

    EXPECT_GE( std::count_if( collectedDataList[0]->signals.begin(),
                              collectedDataList[0]->signals.end(),
                              []( CollectedSignal s ) {
                                  return s.signalID == 2222;
                              } ),
               1 );

    EXPECT_GE( std::count_if( collectedDataList[1]->signals.begin(),
                              collectedDataList[1]->signals.end(),
                              []( CollectedSignal s ) {
                                  return s.signalID == 1111;
                              } ),
               1 );
}

TEST_F( CollectionInspectionEngineDoubleTest, Subsampling )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    // minimumSampleIntervalMs=10 means faster signals are dropped
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 1, 0.2 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 9, 0.3 );
    // As subsampling is 10 this value should be sampled again
    engine.addNewSignal<double>( s1.signalID, timestamp + 10, 0.4 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 40, 0.5 );

    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 0.5 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.4 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );
}

// Only valid when sendDataOnlyOncePerCondition is defined true
TEST_F( CollectionInspectionEngineDoubleTest, SendoutEverySignalOnlyOnce )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    // if this is not 0 the samples that come too late will be dropped
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;

    auto res1 = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( res1, nullptr );
    EXPECT_EQ( res1->signals.size(), 1 );

    engine.evaluateConditions( timestamp + 10000 );
    auto res2 = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs );
    ASSERT_NE( res2, nullptr );
    EXPECT_EQ( res2->signals.size(), 0 );
    // Very old element in queue gets pushed
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );

    engine.evaluateConditions( timestamp + 20000 );
    auto res3 = engine.collectNextDataToSend( timestamp + 20000, waitTimeMs );
    ASSERT_NE( res3, nullptr );
    EXPECT_EQ( res3->signals.size(), 1 );
}

TYPED_TEST( CollectionInspectionEngineTest, HearbeatInterval )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = getSignalType<TypeParam>();
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    this->collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    this->collectionSchemes->conditions[0].condition = this->getAlwaysTrueCondition().get();

    engine.onChangeInspectionMatrix( this->consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<TypeParam>( s1.signalID, timestamp, 11 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    // it should have triggered and data should be available
    EXPECT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    engine.addNewSignal<TypeParam>( s1.signalID, timestamp + 500, 11 );
    engine.evaluateConditions( timestamp + 500 );

    // No new data because heartbeat did not trigger less than 10 seconds passed
    EXPECT_EQ( engine.collectNextDataToSend( timestamp + 500, waitTimeMs ), nullptr );

    engine.addNewSignal<TypeParam>( s1.signalID, timestamp + 10000, 11 );
    engine.evaluateConditions( timestamp + 10000 );

    // heartbeat MinimumPublishIntervalMs=10seconds is over so again data
    EXPECT_NE( engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TwoCollectionSchemesWithDifferentNumberOfSamplesToCollect )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 1235;
    s2.sampleBufferSize = 30;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    // Collection Scheme 2 collects the same signal with the same minimumSampleIntervalMs
    // but wants to publish 100 instead of 50 samples
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = s1.signalID;
    s3.sampleBufferSize = 500;
    s3.minimumSampleIntervalMs = s1.minimumSampleIntervalMs;
    s3.fixedWindowPeriod = 77777;
    InspectionMatrixSignalCollectionInfo s4{};
    s4.signalID = s2.signalID;
    s4.sampleBufferSize = 300;
    s4.minimumSampleIntervalMs = s1.minimumSampleIntervalMs;
    s4.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[1], s3 );
    addSignalToCollect( collectionSchemes->conditions[1], s4 );
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();

    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    for ( int i = 0; i < 10000; i++ )
    {
        timestamp++;
        engine.addNewSignal<double>( s1.signalID, timestamp, i * i );
        engine.addNewSignal<double>( s2.signalID, timestamp, i + 2 );
    }
    engine.evaluateConditions( timestamp );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    auto collectedData2 = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    ASSERT_NE( collectedData, nullptr );
    ASSERT_NE( collectedData2, nullptr );

    EXPECT_EQ( collectedData->signals.size(), 80 );
    ASSERT_EQ( collectedData2->signals.size(), 800 );
    // Currently this 800 signals contain the 80 signals again so the same data is sent to cloud
    // twice by different collection schemes, otherwise this would have only 720 samples
}

TEST_F( CollectionInspectionEngineDoubleTest, TwoSignalsInConditionAndOneSignalToCollect )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 3;
    s3.sampleBufferSize = 50;
    s3.minimumSampleIntervalMs = 0;
    s3.fixedWindowPeriod = 77777;
    s3.isConditionOnlySignal = false;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );
    addSignalToCollect( collectionSchemes->conditions[0], s3 );

    // The condition is (signalID(1)>-100) && (signalID(2)>-500)
    collectionSchemes->conditions[0].condition =
        getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<double>( s3.signalID, timestamp, 1000.0 );
    engine.addNewSignal<double>( s3.signalID, timestamp, 2000.0 );
    engine.addNewSignal<double>( s3.signalID, timestamp, 3000.0 );

    // Signals for condition are not available yet so collectionScheme should not trigger
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s1.signalID, timestamp, -90.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -1000.0 );

    // Condition only for first signal is fulfilled (-90 > -100) but second not so boolean and is false
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s2.signalID, timestamp, -480.0 );

    // Condition is fulfilled so it should trigger
    engine.evaluateConditions( timestamp );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );
    ASSERT_EQ( collectedData->signals[0].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[1].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[2].signalID, s3.signalID );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );
}

// Default is to send data only out once per collectionScheme
TYPED_TEST( CollectionInspectionEngineTest, SendOutEverySignalOnlyOncePerCollectionScheme )
{
    CollectionInspectionEngine engine( true );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = getSignalType<TypeParam>();
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    this->collectionSchemes->conditions[0].minimumPublishIntervalMs = 5000;
    this->collectionSchemes->conditions[0].condition = this->getAlwaysTrueCondition().get();

    engine.onChangeInspectionMatrix( this->consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<TypeParam>( s1.signalID, timestamp, 10 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;

    auto res1 = engine.collectNextDataToSend( timestamp + 1, waitTimeMs );
    ASSERT_NE( res1, nullptr );
    EXPECT_EQ( res1->signals.size(), 1 );

    engine.evaluateConditions( timestamp + 10000 );
    auto res2 = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs );
    ASSERT_NE( res2, nullptr );
    EXPECT_EQ( res2->signals.size(), 0 );

    engine.addNewSignal<TypeParam>( s1.signalID, timestamp + 15000, 10 );

    engine.evaluateConditions( timestamp + 20000 );
    auto res3 = engine.collectNextDataToSend( timestamp + 20000, waitTimeMs );
    ASSERT_NE( res3, nullptr );
    EXPECT_EQ( res3->signals.size(), 1 );
}

TEST_F( CollectionInspectionEngineDoubleTest, SendOutEverySignalNotOnlyOncePerCollectionScheme )
{
    CollectionInspectionEngine engine( false );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 5000;
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;

    auto res1 = engine.collectNextDataToSend( timestamp + 1, waitTimeMs );
    ASSERT_NE( res1, nullptr );
    EXPECT_EQ( res1->signals.size(), 1 );

    engine.evaluateConditions( timestamp + 10000 );
    auto res2 = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs );
    ASSERT_NE( res2, nullptr );
    EXPECT_EQ( res2->signals.size(), 1 );

    engine.addNewSignal<double>( s1.signalID, timestamp + 15000, 0.1 );

    engine.evaluateConditions( timestamp + 20000 );
    auto res3 = engine.collectNextDataToSend( timestamp + 20000, waitTimeMs );
    ASSERT_NE( res3, nullptr );
    EXPECT_EQ( res3->signals.size(), 2 );
}

TEST_F( CollectionInspectionEngineDoubleTest, MoreCollectionSchemesThanSupported )
{
    CollectionInspectionEngine engine( false );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    collectionSchemes->conditions.resize( MAX_NUMBER_OF_ACTIVE_CONDITION + 1 );
    for ( uint32_t i = 0; i < MAX_NUMBER_OF_ACTIVE_CONDITION; i++ )
    {
        collectionSchemes->conditions[i].condition = getAlwaysTrueCondition().get();
        collectionSchemes->conditions[i].probabilityToSend = 1.0;
        addSignalToCollect( collectionSchemes->conditions[i], s1 );
    }
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
    engine.evaluateConditions( timestamp );
    engine.collectNextDataToSend( timestamp, waitTimeMs );
    timestamp += 10000;
    engine.evaluateConditions( timestamp );
    engine.collectNextDataToSend( timestamp, waitTimeMs );
    timestamp += 10000;
    engine.evaluateConditions( timestamp );
    engine.collectNextDataToSend( timestamp, waitTimeMs );
}

/**
 * @brief This test aims to test Inspection Engine to evaluate Geohash Function Node.
 * Here's the test procedure:
 * 1. Generate AST Tree with Root setting to Geohash Function Node
 * 2. Before Signal become ready, we ask Inspection Engine to evaluate Geohash. This Step verify
 *    Inspection Engine's ability to handle corner case if GPS signal is not there
 * 3. Then we add valid lat/lon signal and expect inspection engine to evaluate true
 * 4. Next we change GPS signal slightly. Since Geohash didn't change at given precision.
 *  Evaluation return false.
 * 5. We change GPS signal more so that Geohash changed at given precision. Evaluation return true.
 * 6. As final step, we supply invalid lat/lon to test Inspection Engine can gracefully handle
 *  invalid signal
 */
TEST_F( CollectionInspectionEngineDoubleTest, GeohashFunctionNodeTrigger )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo lat{};
    lat.signalID = 1;
    lat.sampleBufferSize = 50;
    lat.minimumSampleIntervalMs = 0;
    lat.fixedWindowPeriod = 77777;
    lat.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo lon{};
    lon.signalID = 2;
    lon.sampleBufferSize = 50;
    lon.minimumSampleIntervalMs = 0;
    lon.fixedWindowPeriod = 77777;
    lon.isConditionOnlySignal = true;
    addSignalToCollect( collectionSchemes->conditions[0], lat );
    addSignalToCollect( collectionSchemes->conditions[0], lon );

    collectionSchemes->conditions[0].condition = getGeohashFunctionCondition( lat.signalID, lon.signalID, 5 ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // Before GPS signal is ready, we ask Inspection Engine to evaluate Geohash
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_EQ( collectedData, nullptr );

    timestamp += 1000;
    engine.addNewSignal<double>( lat.signalID, timestamp, 37.371392 );
    engine.addNewSignal<double>( lon.signalID, timestamp, -122.046208 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_TRUE( collectedData->mGeohashInfo.hasItems() );
    ASSERT_EQ( collectedData->mGeohashInfo.mGeohashString, "9q9hwg28j" );
    ASSERT_EQ( collectedData->mGeohashInfo.mPrevReportedGeohashString.length(), 0 );

    // 37.361392, -122.056208 -> 9q9hw93mu. still within the same geohash tile at precision 5.
    timestamp += 1000;
    engine.addNewSignal<double>( lat.signalID, timestamp, 37.361392 );
    engine.addNewSignal<double>( lon.signalID, timestamp, -122.056208 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_EQ( collectedData, nullptr );

    // 37.351392, -122.066208 -> 9q9hqkpbp. Geohash changed at precision 5.
    timestamp += 1000;
    engine.addNewSignal<double>( lat.signalID, timestamp, 37.351392 );
    engine.addNewSignal<double>( lon.signalID, timestamp, -122.066208 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );
    ASSERT_TRUE( collectedData->mGeohashInfo.hasItems() );
    ASSERT_EQ( collectedData->mGeohashInfo.mGeohashString, "9q9hqrd5e" );
    ASSERT_EQ( collectedData->mGeohashInfo.mPrevReportedGeohashString, "9q9hwg28j" );

    // We supply an invalid latitude
    timestamp += 1000;
    engine.addNewSignal<double>( lat.signalID, timestamp, 137.351392 );
    engine.addNewSignal<double>( lon.signalID, timestamp, -122.066208 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_EQ( collectedData, nullptr );

    // We supply an invalid longitude
    timestamp += 1000;
    engine.addNewSignal<double>( lat.signalID, timestamp, 37.351392 );
    engine.addNewSignal<double>( lon.signalID, timestamp, -222.066208 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_EQ( collectedData, nullptr );
}

TYPED_TEST( CollectionInspectionEngineTest, CollectWithAfterTime )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 3;
    s3.sampleBufferSize = 3;
    s3.minimumSampleIntervalMs = 0;
    s3.fixedWindowPeriod = 77777;
    s3.isConditionOnlySignal = false;
    s3.signalType = getSignalType<TypeParam>();
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s2 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s3 );

    // The condition is (signalID(1)>-100) && (signalID(2)>-500)
    this->collectionSchemes->conditions[0].condition =
        this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();

    // After the collectionScheme triggered signals should be collected for 2 more seconds
    this->collectionSchemes->conditions[0].afterDuration = 2000;
    engine.onChangeInspectionMatrix( this->consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    TypeParam val1 = 10;
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, val1 );
    engine.addNewSignal<double>( s1.signalID, timestamp, -90.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -1000.0 );
    // Condition not fulfilled so should not trigger
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
    EXPECT_GE( waitTimeMs, 2000 );

    timestamp += 1000;
    TimePoint timestamp0 = timestamp;
    TypeParam val2 = 20;
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, val2 );
    engine.addNewSignal<double>( s1.signalID, timestamp, -90.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -480.0 );
    // Condition fulfilled so should trigger but afterTime not over yet
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
    EXPECT_EQ( waitTimeMs, 2000 );

    timestamp += 1000;
    TimePoint timestamp1 = timestamp;
    TypeParam val3 = 30;
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, val3 );
    engine.addNewSignal<double>( s1.signalID, timestamp, -95.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -485.0 );
    // Condition still fulfilled but already triggered. After time still not over
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
    EXPECT_EQ( waitTimeMs, 1000 );

    timestamp += 500;
    TimePoint timestamp2 = timestamp;
    TypeParam val4 = 40;
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, val4 );
    engine.addNewSignal<double>( s1.signalID, timestamp, -9000.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -9000.0 );
    // Condition not fulfilled anymore but still waiting for afterTime
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    timestamp += 500;
    TimePoint timestamp3 = timestamp;
    TypeParam val5 = 50;
    engine.addNewSignal<TypeParam>( s3.signalID, timestamp, val5 );
    engine.addNewSignal<double>( s1.signalID, timestamp, -9100.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, -9100.0 );
    // Condition not fulfilled. After Time is over so data should be sent out
    engine.evaluateConditions( timestamp );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );
    ASSERT_NE( collectedData, nullptr );

    // sampleBufferSize of the only collected signal s3 is 3
    ASSERT_EQ( collectedData->signals.size(), 3 );
    ASSERT_TRUE( this->compareSignalValue( collectedData->signals[0].getValue(), val5 ) );
    ASSERT_EQ( collectedData->signals[0].receiveTime, timestamp3.systemTimeMs );
    ASSERT_TRUE( this->compareSignalValue( collectedData->signals[1].getValue(), val4 ) );
    ASSERT_EQ( collectedData->signals[1].receiveTime, timestamp2.systemTimeMs );
    ASSERT_TRUE( this->compareSignalValue( collectedData->signals[2].getValue(), val3 ) );
    ASSERT_EQ( collectedData->signals[2].receiveTime, timestamp1.systemTimeMs );
    ASSERT_EQ( collectedData->triggerTime, timestamp0.systemTimeMs );
}

TEST_F( CollectionInspectionEngineDoubleTest, ProbabilityToSendTest )
{

    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    // Set probability to send to 50%
    collectionSchemes->conditions[0].probabilityToSend = 0.5;

    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    const uint32_t NR_OF_HEARTBEAT_INTERVALS = 1000;

    uint64_t numberOfDataToSend = 0;

    for ( uint32_t i = 0; i < NR_OF_HEARTBEAT_INTERVALS; i++ )
    {
        engine.addNewSignal<double>( s1.signalID, timestamp, 0.1 );
        engine.evaluateConditions( timestamp );
        if ( engine.collectNextDataToSend( timestamp, waitTimeMs ) != nullptr )
        {
            numberOfDataToSend++;
        }
        // Increase time stamp by heartbeat interval to trigger new message
        timestamp += collectionSchemes->conditions[0].minimumPublishIntervalMs;
    }

    // Probability to get less than 50 or more than NR_OF_HEARTBEAT_INTERVALS-50 is small if
    // NR_OF_HEARTBEAT_INTERVALS is >> 100. But there is a small chance of this failing even if
    // everything works correctly
    EXPECT_GE( numberOfDataToSend, 50 );
    EXPECT_LE( numberOfDataToSend, NR_OF_HEARTBEAT_INTERVALS - 50 );

    std::cout << NR_OF_HEARTBEAT_INTERVALS << " data to send with a probability of 50%: " << numberOfDataToSend
              << " were actually sent" << std::endl;
}

TEST_F( CollectionInspectionEngineDoubleTest, AvgWindowCondition )
{
    CollectionInspectionEngine engine;
    // fixedWindowPeriod means that we collect data over 300 seconds
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 300000;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: LAST_FIXED_WINDOW_AVG(SignalID(1234)) > -50.0
    collectionSchemes->conditions[0].condition = getLastAvgWindowBiggerCondition( s1.signalID, -50.0 ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    double currentValue = -110.0f;
    double increasePerSample = ( 100.0 ) / static_cast<double>( s1.fixedWindowPeriod );
    uint32_t waitTimeMs = 0;
    // increase value slowly from -110.0 to -10.0 so average is -60
    for ( uint32_t i = 0; i < s1.fixedWindowPeriod; i++ )
    {
        timestamp++;
        currentValue += increasePerSample;
        engine.addNewSignal<double>( s1.signalID, timestamp, currentValue );
        engine.evaluateConditions( timestamp );
        ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
    }

    currentValue = -80.0f;
    increasePerSample = ( 100.0 ) / static_cast<double>( s1.fixedWindowPeriod );
    // increase the value slowly from -80 to 20.0f so avg is -30.0
    for ( uint32_t i = 0; i < s1.fixedWindowPeriod - 1; i++ )
    {
        timestamp++;
        currentValue += increasePerSample;
        engine.addNewSignal<double>( s1.signalID, timestamp, currentValue );
        engine.evaluateConditions( timestamp );
        ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
    }
    // avg -30 is bigger than -50 so condition fulfilled
    engine.evaluateConditions( timestamp + 2 );
    ASSERT_NE( engine.collectNextDataToSend( timestamp + 2, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, PrevLastAvgWindowCondition )
{
    CollectionInspectionEngine engine;
    // fixedWindowPeriod means that we collect data over 300 seconds
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 300000;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: PREV_LAST_FIXED_WINDOW_AVG(SignalID(1234)) > -50.0
    collectionSchemes->conditions[0].condition = getPrevLastAvgWindowBiggerCondition( s1.signalID, -50.0 ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    // Fill the prev last window with 0.0
    for ( uint32_t i = 0; i < s1.fixedWindowPeriod; i++ )
    {
        timestamp++;
        engine.addNewSignal<double>( s1.signalID, timestamp, 0.0 );
        engine.evaluateConditions( timestamp );
        ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
    }
    // No samples arrive for two window2:
    timestamp += s1.fixedWindowPeriod * 2;
    // One more arrives, prev last average is still 0 which is > -50
    engine.addNewSignal<double>( s1.signalID, timestamp, -100.0 );
    engine.evaluateConditions( timestamp );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultiWindowCondition )
{
    CollectionInspectionEngine engine;
    // fixedWindowPeriod means that we collect data over 300 seconds
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: (((LAST_FIXED_WINDOW_MAX(SignalID(1234)) - PREV_LAST_FIXED_WINDOW_MAX(SignalID(1234)))
    //                + PREV_LAST_FIXED_WINDOW_MAX(SignalID(1234)))
    //               < (LAST_FIXED_WINDOW_MAX(SignalID(1234)) * PREV_LAST_FIXED_WINDOW_MAX(SignalID(1234))))
    //              || (LAST_FIXED_WINDOW_MIN(SignalID(1234)) == PREV_LAST_FIXED_WINDOW_MIN(SignalID(1234)))
    collectionSchemes->conditions[0].condition = getMultiFixedWindowCondition( s1.signalID ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    engine.addNewSignal<double>( s1.signalID, timestamp, -95.0 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 50, 100.0 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 70, 110.0 );
    // Condition still fulfilled but already triggered. After time still not over
    engine.evaluateConditions( timestamp + 70 );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp + 70, waitTimeMs ), nullptr );

    engine.addNewSignal<double>( s1.signalID, timestamp + 100, -205.0 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 150, -300.0 );
    engine.addNewSignal<double>( s1.signalID, timestamp + 200, +30.0 );
    engine.evaluateConditions( timestamp + 200 );
    ASSERT_NE( engine.collectNextDataToSend( timestamp + 200, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TestNotEqualOperator )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 456;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );

    // function is: !(!(SignalID(123) <= 0.001) && !((SignalID(123) / SignalID(456)) >= 0.5))
    collectionSchemes->conditions[0].condition = getNotEqualCondition( s1.signalID, s2.signalID ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // Expression should be false because they are equal
    engine.addNewSignal<double>( s1.signalID, timestamp, 100.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, 100.0 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    // Expression should be true: because they are not equal
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, timestamp, 50 );
    engine.evaluateConditions( timestamp );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    // Expression should be false because they are equal again
    timestamp++;
    engine.addNewSignal<double>( s2.signalID, timestamp, 50.0 );
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TwoSignalsRatioCondition )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 456;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );

    // function is: !(!(SignalID(123) <= 0.001) && !((SignalID(123) / SignalID(456)) >= 0.5))
    collectionSchemes->conditions[0].condition =
        getTwoSignalsRatioCondition( s1.signalID, 0.001, s2.signalID, 0.5 ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // Expression should be false: (1.0 <= 0.001) || (1.0 / 100.0) >= 0.5)
    engine.addNewSignal<double>( s1.signalID, timestamp, 1.0 );
    engine.addNewSignal<double>( s2.signalID, timestamp, 100.0 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    // Expression should be true: (0.001 <= 0.001) || (0.001 / 100.0) >= 0.5)
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, timestamp, 0.001 );
    engine.evaluateConditions( timestamp );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    // Expression should be false: (1.0 <= 0.001) || (1.0 / 100.0) >= 0.5)
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, timestamp, 1.0 );
    engine.evaluateConditions( timestamp );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );

    // Expression should be true: (50.0 <= 0.001) || (50.0 / 100.0) >= 0.5)
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, timestamp, 50.0 );
    engine.evaluateConditions( timestamp );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, UnknownExpressionNode )
{
    CollectionInspectionEngine engine;
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: (SignalID(123) <= Unknown)
    collectionSchemes->conditions[0].condition = getUnknownCondition( s1.signalID ).get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    TimePoint timestamp = { 160000000, 100 };
    // Expression should be false: (1.0 <= Unknown)
    engine.addNewSignal<double>( s1.signalID, timestamp, 1.0 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ), nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, RequestTooMuchMemorySignals )
{
    CollectionInspectionEngine engine;
    // for each of the .sampleBufferSize=1000000 multiple bytes have to be allocated
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 1000000;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 300000;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );
}

TEST_F( CollectionInspectionEngineDoubleTest, RequestTooMuchMemoryFrames )
{
    CollectionInspectionEngine engine;
    InspectionMatrixCanFrameCollectionInfo c1;
    c1.frameID = 0x380;
    c1.channelID = 3;
    c1.sampleBufferSize = 1000000;
    c1.minimumSampleIntervalMs = 0;
    collectionSchemes->conditions[0].canFrames.push_back( c1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    engine.onChangeInspectionMatrix( consCollectionSchemes );
}

/*
 * This test is also used for performance analysis. The add new signal takes > 100ns
 * The performance varies if number of signal, number of conditions is changed.
 */
TEST_F( CollectionInspectionEngineDoubleTest, RandomDataTest )
{
    const int NUMBER_OF_COLLECTION_SCHEMES = 200;
    const int NUMBER_OF_SIGNALS = 20000;

    // Keep the seed static so multiple runs are comparable
    std::default_random_engine eng{ static_cast<long unsigned int>( 1620336094 ) };
    std::mt19937 gen{ eng() };
    std::uniform_int_distribution<> uniformDistribution( 0, NUMBER_OF_COLLECTION_SCHEMES - 1 );
    std::normal_distribution<> normalDistribution{ 10, 4 };

    for ( int i = 0; i < NUMBER_OF_COLLECTION_SCHEMES; i++ )
    {

        ConditionWithCollectedData collectionScheme;
        collectionSchemes->conditions.resize( NUMBER_OF_COLLECTION_SCHEMES );
        collectionSchemes->conditions[i].condition = getAlwaysTrueCondition().get();
        collectionSchemes->conditions[i].probabilityToSend = 1.0;
    }

    CollectionInspectionEngine engine;
    // for each of the .sampleBufferSize=1000000 multiple bytes have to be allocated
    std::normal_distribution<> fixedWindowSizeGenerator( 300000, 100000 );
    int minWindow = std::numeric_limits<int>::max();
    int maxWindow = std::numeric_limits<int>::min();
    const int MINIMUM_WINDOW_SIZE = 500000; // should be 500000
    int withoutWindow = 0;
    for ( int i = 0; i < NUMBER_OF_SIGNALS; i++ )
    {

        int windowSize = static_cast<int>( fixedWindowSizeGenerator( gen ) );
        if ( windowSize < MINIMUM_WINDOW_SIZE )
        { // most signals dont have a window
            windowSize = 0;
            withoutWindow++;
        }
        else
        {
            minWindow = std::min( windowSize, minWindow );
            maxWindow = std::max( windowSize, maxWindow );
        }
        InspectionMatrixSignalCollectionInfo s1{};
        s1.signalID = i;
        s1.sampleBufferSize = 10;
        s1.minimumSampleIntervalMs = 1;
        s1.fixedWindowPeriod = windowSize;
        for ( int j = 0; j < normalDistribution( gen ); j++ )
        {
            auto &collectionScheme = collectionSchemes->conditions[uniformDistribution( gen )];
            collectionScheme.signals.push_back( s1 );

            // exactly two signals are added now so add them to condition:
            if ( collectionScheme.signals.size() == 2 )
            {
                if ( uniformDistribution( gen ) < NUMBER_OF_COLLECTION_SCHEMES / 2 )
                {
                    collectionScheme.condition = getLastAvgWindowBiggerCondition( s1.signalID, 15 ).get();
                }
                else
                {
                    collectionScheme.condition =
                        getTwoSignalsBiggerCondition( s1.signalID, 17.5, collectionScheme.signals[0].signalID, 17.5 )
                            .get();
                }
            }
        }
    }
    std::cout << "\nFinished generation " << NUMBER_OF_SIGNALS << " signals and "
              << ( NUMBER_OF_SIGNALS - withoutWindow )
              << " of them with window with a window sampling size varies from " << minWindow << " to " << maxWindow
              << std::endl;
    engine.onChangeInspectionMatrix( consCollectionSchemes );

    const int TIME_TO_SIMULATE_IN_MS = 5000;
    const int SIGNALS_PER_MS = 20;
    TimePoint START_TIMESTAMP = { 160000000, 100 };
    TimePoint timestamp = START_TIMESTAMP;
    uint32_t counter = 0;
    uint32_t dataCollected = 0;

    std::default_random_engine eng2{ static_cast<long unsigned int>( 1620337567 ) };
    std::mt19937 gen2{ eng() };
    std::uniform_int_distribution<> signalIDGenerator( 0, NUMBER_OF_SIGNALS - 1 );
    for ( int i = 0; i < TIME_TO_SIMULATE_IN_MS * SIGNALS_PER_MS; i++ )
    {
        if ( i % ( ( TIME_TO_SIMULATE_IN_MS * SIGNALS_PER_MS ) / 100 ) == 0 )
        {
            std::cout << "." << std::flush;
        }
        counter++;
        bool tickIncreased = false;
        if ( counter == SIGNALS_PER_MS )
        {
            timestamp++;
            counter = 0;
            tickIncreased = true;
        }
        engine.addNewSignal<double>( signalIDGenerator( gen2 ), timestamp, normalDistribution( gen ) );
        if ( tickIncreased )
        {
            uint32_t waitTimeMs = 0;
            engine.evaluateConditions( timestamp );
            while ( engine.collectNextDataToSend( timestamp, waitTimeMs ) != nullptr )
            {
                dataCollected++;
            }
        }
    }
    std::cout << "\nSimulated " << TIME_TO_SIMULATE_IN_MS / 1000 << " seconds with " << SIGNALS_PER_MS
              << " signals arriving per millisecond. Avg "
              << ( static_cast<double>( dataCollected ) ) / ( static_cast<double>( TIME_TO_SIMULATE_IN_MS / 1000 ) )
              << " collected data every second" << std::endl;
}

TEST_F( CollectionInspectionEngineDoubleTest, NoCollectionSchemes )
{
    CollectionInspectionEngine engine;
    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
}

} // namespace IoTFleetWise
} // namespace Aws
