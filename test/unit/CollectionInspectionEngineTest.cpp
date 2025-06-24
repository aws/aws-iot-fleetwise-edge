// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CollectionInspectionEngine.h"
#include "CollectionSchemeManagerTest.h"
#include "Testing.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CollectionSchemeIngestion.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/LogLevel.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "collection_schemes.pb.h"
#include <algorithm>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "CollectionSchemeManagerMock.h"
#include "SenderMock.h"
#include "StreamForwarderMock.h"
#include "StreamManagerMock.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include "common_types.pb.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::StrictMock;
using signalTypes =
    ::testing::Types<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double>;

template <typename T>
class CollectionInspectionEngineTest : public ::testing::Test
{
protected:
    std::shared_ptr<InspectionMatrix> collectionSchemes;
    std::shared_ptr<const InspectionMatrix> consCollectionSchemes;
    std::vector<std::shared_ptr<ExpressionNode>> expressionNodes;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    CANInterfaceIDTranslator mCANIDTranslator;
    ::testing::StrictMock<Testing::StreamManagerMock> mStreamManager;
    RateLimiter mRateLimiter;
    ::testing::StrictMock<Testing::SenderMock> mMqttSender;
    PayloadAdaptionConfig mPayloadAdaptionConfigUncompressed{ 80, 70, 90, 10 };
    PayloadAdaptionConfig mPayloadAdaptionConfigCompressed{ 80, 70, 90, 10 };
    TelemetryDataSender mTelemetryDataSender;
    Testing::StreamForwarderMock mStreamForwarder;
    static constexpr unsigned MAXIMUM_PAYLOAD_SIZE = 400;
#endif

    CollectionInspectionEngineTest()
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        : mTelemetryDataSender(
              [this]() -> ISender & {
                  EXPECT_CALL( mMqttSender, getMaxSendSize() )
                      .Times( ::testing::AnyNumber() )
                      .WillRepeatedly( ::testing::Return( MAXIMUM_PAYLOAD_SIZE ) );
                  return mMqttSender;
              }(),
              std::make_unique<DataSenderProtoWriter>( mCANIDTranslator, nullptr ),
              mPayloadAdaptionConfigUncompressed,
              mPayloadAdaptionConfigCompressed )
        , mStreamForwarder( mStreamManager, mTelemetryDataSender, mRateLimiter )
#endif
    {
    }

    static void
    convertSchemes( std::vector<Schemas::CollectionSchemesMsg::CollectionScheme> schemes,
                    std::shared_ptr<ICollectionSchemeList> &result )
    {
        std::vector<std::shared_ptr<ICollectionScheme>> collectionSchemes;
        for ( auto scheme : schemes )
        {
            auto campaign = std::make_shared<CollectionSchemeIngestion>(
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                std::make_shared<CollectionSchemeIngestion::PartialSignalIDLookup>()
#endif
            );
            campaign->copyData( std::make_shared<Schemas::CollectionSchemesMsg::CollectionScheme>( scheme ) );
            ASSERT_TRUE( campaign->build() );
            collectionSchemes.emplace_back( campaign );
        }
        result = std::make_shared<ICollectionSchemeListTest>( collectionSchemes );
    }

    static void
    convertScheme( Schemas::CollectionSchemesMsg::CollectionScheme scheme,
                   std::shared_ptr<ICollectionSchemeList> &result )
    {
        convertSchemes( std::vector<Schemas::CollectionSchemesMsg::CollectionScheme>{ scheme }, result );
    }

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
        case SignalType::STRING:
            return static_cast<RawData::BufferHandle>( sigVal ) == signalValueWrapper.value.uint32Val;
        case SignalType::UNKNOWN:
            return false;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        case SignalType::COMPLEX_SIGNAL:
            return static_cast<RawData::BufferHandle>( sigVal ) == signalValueWrapper.value.uint32Val;
#endif
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
    getEqualCondition( SignalID id1, SignalID id2 )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto equal = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal1 = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signal2 = expressionNodes.back();

        signal1->nodeType = ExpressionNodeType::SIGNAL;
        signal1->signalID = id1;

        signal2->nodeType = ExpressionNodeType::SIGNAL;
        signal2->signalID = id2;

        equal->nodeType = ExpressionNodeType::OPERATOR_EQUAL;
        equal->left = signal1.get();
        equal->right = signal2.get();
        return ( equal );
    }

    std::shared_ptr<ExpressionNode>
    getStringCondition( std::string nodeLeftValue, std::string nodeRightValue, ExpressionNodeType expressionOperation )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto nodeOperation = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto nodeLeft = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto nodeRight = expressionNodes.back();

        nodeLeft->nodeType = ExpressionNodeType::STRING;
        nodeLeft->stringValue = nodeLeftValue;

        nodeRight->nodeType = ExpressionNodeType::STRING;
        nodeRight->stringValue = nodeRightValue;

        nodeOperation->nodeType = expressionOperation;
        nodeOperation->left = nodeLeft.get();
        nodeOperation->right = nodeRight.get();
        return ( nodeOperation );
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
    getOneSignalBiggerCondition( SignalID id1, double threshold1 )
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

        lastMin1->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
        lastMin1->function.windowFunction = WindowFunction::LAST_FIXED_WINDOW_MIN;
        lastMin1->signalID = id1;

        previousLastMin1->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
        previousLastMin1->function.windowFunction = WindowFunction::PREV_LAST_FIXED_WINDOW_MIN;
        previousLastMin1->signalID = id1;

        lastMax1->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
        lastMax1->function.windowFunction = WindowFunction::LAST_FIXED_WINDOW_MAX;
        lastMax1->signalID = id1;

        previousLastMax1->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
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
    getCustomFunctionCondition( SignalID id1, const std::string &name )
    {
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto customFunctionNode = expressionNodes.back();
        expressionNodes.push_back( std::make_shared<ExpressionNode>() );
        auto signalNode = expressionNodes.back();

        signalNode->nodeType = ExpressionNodeType::SIGNAL;
        signalNode->signalID = id1;
        customFunctionNode->nodeType = ExpressionNodeType::CUSTOM_FUNCTION;
        customFunctionNode->function.customFunctionName = name;
        customFunctionNode->function.customFunctionParams.push_back( signalNode.get() );
        return customFunctionNode;
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

        function1->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
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

        function1->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
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
        collectionSchemes->conditions[0].isStaticCondition = true;
        collectionSchemes->conditions[1].condition = getAlwaysFalseCondition().get();
        collectionSchemes->conditions[1].isStaticCondition = true;
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
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    s2.signalType = SignalType::DOUBLE;
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
    // Condition contains signals
    this->collectionSchemes->conditions[0].isStaticCondition = false;
    this->collectionSchemes->conditions[0].condition =
        this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    TypeParam testVal1 = 10;
    TypeParam testVal2 = 20;
    TypeParam testVal3 = 30;
    engine.addNewSignal<TypeParam>(
        s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, testVal1 );
    engine.addNewSignal<TypeParam>(
        s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, testVal2 );
    engine.addNewSignal<TypeParam>(
        s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, testVal3 );

    // Signals for condition are not available yet so collectionScheme should not trigger
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 1000;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );

    // Condition only for first signal is fulfilled (-90 > -100) but second not so boolean and is false
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );

    // Condition is fulfilled so it should trigger
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );
    ASSERT_EQ( collectedData->signals[0].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[1].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[2].signalID, s3.signalID );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );
}

#ifdef FWE_FEATURE_STORE_AND_FORWARD
TYPED_TEST( CollectionInspectionEngineTest, ForwardConditionOnly )
{
    EXPECT_CALL( this->mStreamForwarder, beginForward( "arn:1/T0123", 0, Store::StreamForwarder::Source::CONDITION ) )
        .Times( 1 );
    EXPECT_CALL( this->mStreamForwarder, cancelForward( "arn:1/T0123", 0, testing::_ ) ).Times( testing::AtLeast( 1 ) );

    // 1. Define some signals (type, id) which can exist
    InspectionMatrixSignalCollectionInfo s1{ 1, 50, 10, 77777, true, SignalType::DOUBLE, {} };
    InspectionMatrixSignalCollectionInfo s2{ 2, 50, 10, 77777, true, SignalType::DOUBLE, {} };

    // 2. Define the campaign and forward condition
    const SyncID campaignID = "arn:1/T0123";
    this->collectionSchemes->conditions[0].metadata.collectionSchemeID = campaignID;
    this->collectionSchemes->conditions[0].metadata.campaignArn = campaignID;

    // 2a. Define that the above signals should be inspected for our condition
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s2 );

    // 2b. Define the condition expression: (signalID(1)>-100) && (signalID(2)>-500)
    // Condition contains signal
    this->collectionSchemes->conditions[0].isStaticCondition = false;
    ConditionForForward forwardCondition;
    forwardCondition.condition = this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();
    this->collectionSchemes->conditions[0].forwardConditions.push_back( forwardCondition );

    // 3. Boot up the inspection engine
    TimePoint timestamp = { 160000000, 100 };
    CollectionInspectionEngine engine( nullptr, MIN_FETCH_TRIGGER_MS, nullptr, true, &this->mStreamForwarder );
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );

    // 4. Send in some signals which should not cause the condition to trigger
    timestamp += 1000;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );

    // 5. Send in some signals which should cause the condition to trigger
    timestamp += 1000;
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    // 6. Send in some signals which should cause the condition to go back to not triggered
    timestamp += 1000;
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -520.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
}

TYPED_TEST( CollectionInspectionEngineTest, OneCampaignWithForwardAndCollectionConditions )
{
    EXPECT_CALL( this->mStreamForwarder, beginForward( "arn:1/T0123", 0, Store::StreamForwarder::Source::CONDITION ) )
        .Times( 1 );
    EXPECT_CALL( this->mStreamForwarder, cancelForward( "arn:1/T0123", 0, testing::_ ) ).Times( testing::AtLeast( 1 ) );

    // 1. Define some signals (type, id) which can exist
    InspectionMatrixSignalCollectionInfo s1{ 1, 50, 10, 77777, false, SignalType::DOUBLE, {} };
    InspectionMatrixSignalCollectionInfo s2{ 2, 50, 10, 77777, true, SignalType::DOUBLE, {} };

    // Define the campaign id to use
    const SyncID campaignID = "arn:1/T0123";

    // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
    this->collectionSchemes->conditions[0].metadata.collectionSchemeID = campaignID;
    this->collectionSchemes->conditions[0].metadata.campaignArn = campaignID;
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s2 );
    // Condition contains signal
    this->collectionSchemes->conditions[0].isStaticCondition = false;
    ConditionForForward forwardCondition0;
    forwardCondition0.condition = this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();
    this->collectionSchemes->conditions[0].forwardConditions.push_back( forwardCondition0 );

    this->collectionSchemes->conditions[0].condition = this->getOneSignalBiggerCondition( s1.signalID, -100.0 ).get();

    // Boot up the inspection engine
    TimePoint timestamp = { 160000000, 100 };
    CollectionInspectionEngine engine( nullptr, MIN_FETCH_TRIGGER_MS, nullptr, true, &this->mStreamForwarder );
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );

    // Verify the collection condition has not collected anything
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Send in some signals which should cause collection to trigger, but not forward
    timestamp += 1000;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    // Verify we have collected the signal
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );
    ASSERT_EQ( collectedData->signals[0].signalID, s1.signalID );
    ASSERT_EQ( collectedData->signals[0].getValue().value.doubleVal, -90.0 );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );

    // 5. Send in some signals which should cause the condition to trigger
    timestamp += 1000;
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    // 6. Send in some signals which should cause the condition to go back to not triggered
    timestamp += 1000;
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -520.0 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -110.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
}

TYPED_TEST( CollectionInspectionEngineTest, ForwardConditionDefaultsToFalse )
{
    EXPECT_CALL( this->mStreamForwarder, beginForward( "arn:1/T0123", 0, testing::_ ) ).Times( 0 );

    // 1. Define some signals (type, id) which can exist
    InspectionMatrixSignalCollectionInfo s1{ 1, 50, 10, 77777, false, SignalType::DOUBLE, {} };
    InspectionMatrixSignalCollectionInfo s2{ 2, 50, 10, 77777, true, SignalType::DOUBLE, {} };

    // Define the campaign id to use
    const SyncID campaignID = "arn:1/T0123";

    // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
    this->collectionSchemes->conditions[0].metadata.collectionSchemeID = campaignID;
    this->collectionSchemes->conditions[0].metadata.campaignArn = campaignID;
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s2 );
    ConditionForForward forwardCondition0;
    forwardCondition0.condition = this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();
    this->collectionSchemes->conditions[0].forwardConditions.push_back( forwardCondition0 );

    this->collectionSchemes->conditions[0].condition = this->getOneSignalBiggerCondition( s1.signalID, -100.0 ).get();

    // Boot up the inspection engine
    TimePoint timestamp = { 160000000, 100 };
    CollectionInspectionEngine engine( nullptr, MIN_FETCH_TRIGGER_MS, nullptr, true, &this->mStreamForwarder );
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    // Verify the collection condition has not collected anything
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TYPED_TEST( CollectionInspectionEngineTest, RealCampaignWithForwardAndCollectionConditions )
{
    EXPECT_CALL( this->mStreamForwarder, beginForward( "arn:1/T0123", 0, testing::_ ) ).Times( 1 );
    EXPECT_CALL( this->mStreamForwarder, cancelForward( "arn:1/T0123", 0, testing::_ ) ).Times( testing::AtLeast( 1 ) );

    const SignalID signalID1 = 1;
    const SignalID signalID2 = 2;
    uint32_t sampleBufferSize = 50;
    uint32_t minimumSampleIntervalMs = 10;
    uint32_t fixedWindowPeriod = 77777;

    const uint32_t defaultPartitionID = 0;

    InspectionMatrixSignalCollectionInfo s1{
        signalID1, sampleBufferSize, minimumSampleIntervalMs, fixedWindowPeriod, false, SignalType::DOUBLE, {} };
    InspectionMatrixSignalCollectionInfo s2{
        signalID2, sampleBufferSize, minimumSampleIntervalMs, fixedWindowPeriod, true, SignalType::DOUBLE, {} };

    std::shared_ptr<const Clock> clock = ClockHandler::getClock();

    const SyncID campaignID = "arn:1/T0123";
    std::shared_ptr<ICollectionSchemeList> campaignWithSinglePartition;
    {
        Schemas::CollectionSchemesMsg::CollectionScheme scheme;

        scheme.set_campaign_sync_id( campaignID );
        scheme.set_campaign_arn( campaignID );
        scheme.set_decoder_manifest_sync_id( "DM1" );
        scheme.set_expiry_time_ms_epoch( clock->systemTimeSinceEpochMs() + 1000 * 60 );

        // Define the collection condition: (signalID(1)>-100)
        {
            Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme *message =
                scheme.mutable_condition_based_collection_scheme();
            message->set_condition_minimum_interval_ms( 650 );
            message->set_condition_language_version( 20 );
            message->set_condition_trigger_mode(
                Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme_ConditionTriggerMode_TRIGGER_ALWAYS );

            auto *root = new Schemas::CommonTypesMsg::ConditionNode();
            message->set_allocated_condition_tree( root );
            auto *rootOp = new Schemas::CommonTypesMsg::ConditionNode_NodeOperator();
            root->set_allocated_node_operator( rootOp );
            rootOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            //----------

            auto *left = new Schemas::CommonTypesMsg::ConditionNode();
            rootOp->set_allocated_left_child( left );
            left->set_node_signal_id( signalID1 );

            auto *right = new Schemas::CommonTypesMsg::ConditionNode();
            rootOp->set_allocated_right_child( right );
            right->set_node_double_value( -100 );
        }

        auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

        // partition 0
        auto *partition = store_and_forward_configuration->add_partition_configuration();
        auto *storageOptions = partition->mutable_storage_options();
        storageOptions->set_maximum_size_in_bytes( 1000000 );
        storageOptions->set_storage_location( "partition0" );
        storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

        // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
        auto *forwardOptions = new Schemas::CollectionSchemesMsg::UploadOptions();
        partition->set_allocated_upload_options( forwardOptions );
        {
            auto *root = forwardOptions->mutable_condition_tree();
            auto *rootOp = root->mutable_node_operator();
            rootOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND );

            //----------

            auto *left = rootOp->mutable_left_child();
            auto *leftOp = left->mutable_node_operator();
            leftOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            auto *right = rootOp->mutable_right_child();
            auto *rightOp = right->mutable_node_operator();
            rightOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            //----------

            auto *left_left = leftOp->mutable_left_child();
            left_left->set_node_signal_id( signalID1 );

            auto *left_right = leftOp->mutable_right_child();
            left_right->set_node_double_value( -100 );

            auto *right_left = rightOp->mutable_left_child();
            right_left->set_node_signal_id( signalID2 );

            auto *right_right = rightOp->mutable_right_child();
            right_right->set_node_double_value( -500 );
        }

        // map signals to partitions
        auto *signalInformation = scheme.add_signal_information();
        signalInformation->set_signal_id( signalID1 );
        signalInformation->set_data_partition_id( defaultPartitionID );
        signalInformation->set_sample_buffer_size( sampleBufferSize );
        signalInformation->set_minimum_sample_period_ms( minimumSampleIntervalMs );
        signalInformation->set_fixed_window_period_ms( fixedWindowPeriod );
        signalInformation->set_condition_only_signal( false );

        signalInformation = scheme.add_signal_information();
        signalInformation->set_signal_id( signalID2 );
        signalInformation->set_data_partition_id( defaultPartitionID );
        signalInformation->set_sample_buffer_size( sampleBufferSize );
        signalInformation->set_minimum_sample_period_ms( minimumSampleIntervalMs );
        signalInformation->set_fixed_window_period_ms( fixedWindowPeriod );
        signalInformation->set_condition_only_signal( true );

        this->convertScheme( scheme, campaignWithSinglePartition );
    }
    // create inspection matrix based on campaign config
    const std::string decoderManifestID = "DM1";
    CANInterfaceIDTranslator canIDTranslator;
    auto collectionSchemeManager = std::make_shared<CollectionSchemeManagerWrapper>(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), decoderManifestID );
    auto DM1 = std::make_shared<IDecoderManifestTest>( decoderManifestID );
    collectionSchemeManager->onDecoderManifestUpdate( DM1 );
    collectionSchemeManager->onCollectionSchemeUpdate( campaignWithSinglePartition );
    collectionSchemeManager->updateAvailable();
    ASSERT_TRUE( collectionSchemeManager->getmProcessCollectionScheme() );
    collectionSchemeManager->updateMapsandTimeLine( clock->timeSinceEpoch() );
    std::shared_ptr<InspectionMatrix> matrix;
    std::shared_ptr<FetchMatrix> fetchMatrix;
    collectionSchemeManager->generateInspectionMatrix( matrix, fetchMatrix );

    // Boot up the inspection engine
    TimePoint timestamp = { 160000000, 100 };
    std::shared_ptr<CollectionInspectionEngine> engine = std::make_shared<CollectionInspectionEngine>(
        nullptr, MIN_FETCH_TRIGGER_MS, nullptr, true, &this->mStreamForwarder );
    engine->onChangeInspectionMatrix( matrix, timestamp );
    engine->evaluateConditions( timestamp );

    // Verify the collection condition has not collected anything
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine->collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Send in some signals which should cause collection to trigger, but not forward
    timestamp += 1000;
    engine->addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine->addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );
    ASSERT_TRUE( engine->evaluateConditions( timestamp ) );

    // Verify we have collected the signal
    auto collectedData = engine->collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );
    ASSERT_EQ( collectedData->signals[0].signalID, s1.signalID );
    ASSERT_EQ( collectedData->signals[0].getValue().value.doubleVal, -90.0 );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );

    // 5. Send in some signals which should cause the condition to trigger
    timestamp += 1000;
    engine->addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );
    engine->evaluateConditions( timestamp );

    // 6. Send in some signals which should cause the condition to go back to not triggered
    timestamp += 1000;
    engine->addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -520.0 );
    engine->evaluateConditions( timestamp );
}

TYPED_TEST( CollectionInspectionEngineTest, MultipleCampaignsAndPartitionsWithForwardAndTimedCollectionConditions )
{
    const SignalID signalID1 = 1;
    const SignalID signalID2 = 2;
    uint32_t sampleBufferSize = 50;
    uint32_t minimumSampleIntervalMs = 10;
    uint32_t fixedWindowPeriod = 77777;

    const uint32_t defaultPartitionID = 0;
    const uint32_t partitionID1 = 1;

    InspectionMatrixSignalCollectionInfo s1{
        signalID1, sampleBufferSize, minimumSampleIntervalMs, fixedWindowPeriod, false, SignalType::DOUBLE, {} };
    InspectionMatrixSignalCollectionInfo s2{
        signalID2, sampleBufferSize, minimumSampleIntervalMs, fixedWindowPeriod, true, SignalType::DOUBLE, {} };

    std::shared_ptr<const Clock> clock = ClockHandler::getClock();

    const SyncID campaignID1 = "arn:1/T0123";
    Schemas::CollectionSchemesMsg::CollectionScheme scheme1;
    {
        Schemas::CollectionSchemesMsg::CollectionScheme scheme;

        scheme.set_campaign_sync_id( campaignID1 );
        scheme.set_campaign_arn( campaignID1 );
        scheme.set_decoder_manifest_sync_id( "DM1" );
        scheme.set_expiry_time_ms_epoch( clock->systemTimeSinceEpochMs() + 1000 * 60 );

        Schemas::CollectionSchemesMsg::TimeBasedCollectionScheme *message =
            scheme.mutable_time_based_collection_scheme();
        message->set_time_based_collection_scheme_period_ms( 100 );

        auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

        // partition 0
        auto *partition = store_and_forward_configuration->add_partition_configuration();
        auto *storageOptions = partition->mutable_storage_options();
        storageOptions->set_maximum_size_in_bytes( 1000000 );
        storageOptions->set_storage_location( "partition0" );
        storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

        // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
        auto *forwardOptions = new Schemas::CollectionSchemesMsg::UploadOptions();
        partition->set_allocated_upload_options( forwardOptions );
        {
            auto *root = forwardOptions->mutable_condition_tree();
            auto *rootOp = root->mutable_node_operator();
            rootOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND );

            //----------

            auto *left = rootOp->mutable_left_child();
            auto *leftOp = left->mutable_node_operator();
            leftOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            auto *right = rootOp->mutable_right_child();
            auto *rightOp = right->mutable_node_operator();
            rightOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            //----------

            auto *left_left = leftOp->mutable_left_child();
            left_left->set_node_signal_id( signalID1 );

            auto *left_right = leftOp->mutable_right_child();
            left_right->set_node_double_value( -100 );

            auto *right_left = rightOp->mutable_left_child();
            right_left->set_node_signal_id( signalID2 );

            auto *right_right = rightOp->mutable_right_child();
            right_right->set_node_double_value( -500 );
        }

        // partition 2
        partition = store_and_forward_configuration->add_partition_configuration();
        storageOptions = partition->mutable_storage_options();
        storageOptions->set_maximum_size_in_bytes( 1000000 );
        storageOptions->set_storage_location( "partition1" );
        storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

        // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
        forwardOptions = new Schemas::CollectionSchemesMsg::UploadOptions();
        partition->set_allocated_upload_options( forwardOptions );
        {
            auto *root = forwardOptions->mutable_condition_tree();
            auto *rootOp = root->mutable_node_operator();
            rootOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND );

            //----------

            auto *left = rootOp->mutable_left_child();
            auto *leftOp = left->mutable_node_operator();
            leftOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            auto *right = rootOp->mutable_right_child();
            auto *rightOp = right->mutable_node_operator();
            rightOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            //----------

            auto *left_left = leftOp->mutable_left_child();
            left_left->set_node_signal_id( signalID1 );

            auto *left_right = leftOp->mutable_right_child();
            left_right->set_node_double_value( -100 );

            auto *right_left = rightOp->mutable_left_child();
            right_left->set_node_signal_id( signalID2 );

            auto *right_right = rightOp->mutable_right_child();
            right_right->set_node_double_value( -500 );
        }

        // map signals to partitions
        auto *signalInformation = scheme.add_signal_information();
        signalInformation->set_signal_id( signalID1 );
        signalInformation->set_data_partition_id( defaultPartitionID );
        signalInformation->set_sample_buffer_size( sampleBufferSize );
        signalInformation->set_minimum_sample_period_ms( minimumSampleIntervalMs );
        signalInformation->set_fixed_window_period_ms( fixedWindowPeriod );
        signalInformation->set_condition_only_signal( false );

        signalInformation = scheme.add_signal_information();
        signalInformation->set_signal_id( signalID2 );
        signalInformation->set_data_partition_id( partitionID1 );
        signalInformation->set_sample_buffer_size( sampleBufferSize );
        signalInformation->set_minimum_sample_period_ms( minimumSampleIntervalMs );
        signalInformation->set_fixed_window_period_ms( fixedWindowPeriod );
        signalInformation->set_condition_only_signal( true );

        scheme1 = scheme;
    }

    const SyncID campaignID2 = "arn:2/T0456";
    Schemas::CollectionSchemesMsg::CollectionScheme scheme2;
    {
        Schemas::CollectionSchemesMsg::CollectionScheme scheme;

        scheme.set_campaign_sync_id( campaignID2 );
        scheme.set_campaign_arn( campaignID2 );
        scheme.set_decoder_manifest_sync_id( "DM1" );
        scheme.set_expiry_time_ms_epoch( clock->systemTimeSinceEpochMs() + 1000 * 60 );

        Schemas::CollectionSchemesMsg::TimeBasedCollectionScheme *message =
            scheme.mutable_time_based_collection_scheme();
        message->set_time_based_collection_scheme_period_ms( 100 );

        auto *store_and_forward_configuration = scheme.mutable_store_and_forward_configuration();

        // partition 0
        auto *partition = store_and_forward_configuration->add_partition_configuration();
        auto *storageOptions = partition->mutable_storage_options();
        storageOptions->set_maximum_size_in_bytes( 1000000 );
        storageOptions->set_storage_location( "partition0" );
        storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

        // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
        auto *forwardOptions = new Schemas::CollectionSchemesMsg::UploadOptions();
        partition->set_allocated_upload_options( forwardOptions );
        {
            auto *root = forwardOptions->mutable_condition_tree();
            auto *rootOp = root->mutable_node_operator();
            rootOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND );

            //----------

            auto *left = rootOp->mutable_left_child();
            auto *leftOp = left->mutable_node_operator();
            leftOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            auto *right = rootOp->mutable_right_child();
            auto *rightOp = right->mutable_node_operator();
            rightOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            //----------

            auto *left_left = leftOp->mutable_left_child();
            left_left->set_node_signal_id( signalID1 );

            auto *left_right = leftOp->mutable_right_child();
            left_right->set_node_double_value( -100 );

            auto *right_left = rightOp->mutable_left_child();
            right_left->set_node_signal_id( signalID2 );

            auto *right_right = rightOp->mutable_right_child();
            right_right->set_node_double_value( -500 );
        }

        // partition 2
        partition = store_and_forward_configuration->add_partition_configuration();
        storageOptions = partition->mutable_storage_options();
        storageOptions->set_maximum_size_in_bytes( 1000000 );
        storageOptions->set_storage_location( "partition1" );
        storageOptions->set_minimum_time_to_live_in_seconds( 1000000 );

        // Define the forward condition: (signalID(1)>-100) && (signalID(2)>-500)
        forwardOptions = new Schemas::CollectionSchemesMsg::UploadOptions();
        partition->set_allocated_upload_options( forwardOptions );
        {
            auto *root = forwardOptions->mutable_condition_tree();
            auto *rootOp = root->mutable_node_operator();
            rootOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND );

            //----------

            auto *left = rootOp->mutable_left_child();
            auto *leftOp = left->mutable_node_operator();
            leftOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            auto *right = rootOp->mutable_right_child();
            auto *rightOp = right->mutable_node_operator();
            rightOp->set_operator_( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER );

            //----------

            auto *left_left = leftOp->mutable_left_child();
            left_left->set_node_signal_id( signalID1 );

            auto *left_right = leftOp->mutable_right_child();
            left_right->set_node_double_value( -100 );

            auto *right_left = rightOp->mutable_left_child();
            right_left->set_node_signal_id( signalID2 );

            auto *right_right = rightOp->mutable_right_child();
            right_right->set_node_double_value( -500 );
        }

        // map signals to partitions
        auto *signalInformation = scheme.add_signal_information();
        signalInformation->set_signal_id( signalID1 );
        signalInformation->set_data_partition_id( defaultPartitionID );
        signalInformation->set_sample_buffer_size( sampleBufferSize );
        signalInformation->set_minimum_sample_period_ms( minimumSampleIntervalMs );
        signalInformation->set_fixed_window_period_ms( fixedWindowPeriod );
        signalInformation->set_condition_only_signal( false );

        signalInformation = scheme.add_signal_information();
        signalInformation->set_signal_id( signalID2 );
        signalInformation->set_data_partition_id( partitionID1 );
        signalInformation->set_sample_buffer_size( sampleBufferSize );
        signalInformation->set_minimum_sample_period_ms( minimumSampleIntervalMs );
        signalInformation->set_fixed_window_period_ms( fixedWindowPeriod );
        signalInformation->set_condition_only_signal( true );

        scheme2 = scheme;
    }

    EXPECT_CALL( this->mStreamForwarder, beginForward( "arn:1/T0123", testing::AnyOf( 0, 1 ), testing::_ ) ).Times( 4 );
    EXPECT_CALL( this->mStreamForwarder, beginForward( "arn:2/T0456", testing::AnyOf( 0, 1 ), testing::_ ) ).Times( 2 );
    EXPECT_CALL( this->mStreamForwarder, cancelForward( "arn:1/T0123", testing::AnyOf( 0, 1 ), testing::_ ) )
        .Times( ( 12 ) );
    EXPECT_CALL( this->mStreamForwarder, cancelForward( "arn:2/T0456", testing::AnyOf( 0, 1 ), testing::_ ) )
        .Times( ( 6 ) );

    // Boot up the inspection engine
    TimePoint timestamp = { 160000000, 100 };
    std::shared_ptr<CollectionInspectionEngine> engine = std::make_shared<CollectionInspectionEngine>(
        nullptr, MIN_FETCH_TRIGGER_MS, nullptr, true, &this->mStreamForwarder );

    // add decoder manifest
    const std::string decoderManifestID = "DM1";
    CANInterfaceIDTranslator canIDTranslator;
    auto collectionSchemeManager = std::make_shared<Aws::IoTFleetWise::CollectionSchemeManagerWrapper>(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), decoderManifestID );
    auto DM1 = std::make_shared<IDecoderManifestTest>( decoderManifestID );
    collectionSchemeManager->onDecoderManifestUpdate( DM1 );

    // generate inspection matrix based on campaigns
    std::shared_ptr<ICollectionSchemeList> campaigns;
    this->convertSchemes( { scheme1, scheme2 }, campaigns );
    collectionSchemeManager->onCollectionSchemeUpdate( campaigns );
    collectionSchemeManager->updateAvailable();
    ASSERT_TRUE( collectionSchemeManager->getmProcessCollectionScheme() );
    collectionSchemeManager->updateMapsandTimeLine( clock->timeSinceEpoch() );
    std::shared_ptr<InspectionMatrix> matrix;
    std::shared_ptr<FetchMatrix> fetchMatrix;
    collectionSchemeManager->generateInspectionMatrix( matrix, fetchMatrix );
    // add matrix to the inspection engine
    engine->onChangeInspectionMatrix( matrix, timestamp );
    engine->evaluateConditions( timestamp );

    // Verify the collection condition has not collected anything
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine->collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Send in some signals which should cause collection to trigger, but not forward
    timestamp += 1000;
    engine->addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine->addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );
    engine->evaluateConditions( timestamp );

    // Verify we have collected the signal
    auto collectedData = engine->collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );
    ASSERT_EQ( collectedData->signals[0].signalID, s1.signalID );
    ASSERT_EQ( collectedData->signals[0].getValue().value.doubleVal, -90.0 );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );

    // 5. Send in some signals which should cause the condition to trigger
    timestamp += 1000;
    engine->addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );
    engine->evaluateConditions( timestamp );

    // 6. Send in some signals which should cause the condition to go back to not triggered
    timestamp += 1000;
    engine->addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -520.0 );
    engine->evaluateConditions( timestamp );

    // remove a campaign from matrix

    this->convertSchemes( { scheme1 }, campaigns );
    collectionSchemeManager->onCollectionSchemeUpdate( campaigns );
    collectionSchemeManager->updateAvailable();
    ASSERT_TRUE( collectionSchemeManager->getmProcessCollectionScheme() );
    collectionSchemeManager->updateMapsandTimeLine( clock->timeSinceEpoch() );
    collectionSchemeManager->generateInspectionMatrix( matrix, fetchMatrix );
    // add matrix to the inspection engine
    engine->onChangeInspectionMatrix( matrix, timestamp );
    engine->evaluateConditions( timestamp );

    // Verify the collection condition has not collected anything
    waitTimeMs = 0;
    ASSERT_EQ( engine->collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Send in some signals which should cause collection to trigger, but not forward
    timestamp += 1000;
    engine->addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine->addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );
    engine->evaluateConditions( timestamp );

    // Verify we have collected the signal
    collectedData = engine->collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );
    ASSERT_EQ( collectedData->signals[0].signalID, s1.signalID );
    ASSERT_EQ( collectedData->signals[0].getValue().value.doubleVal, -90.0 );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );

    // 5. Send in some signals which should cause the condition to trigger
    timestamp += 1000;
    engine->addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );
    engine->evaluateConditions( timestamp );

    // 6. Send in some signals which should cause the condition to go back to not triggered
    timestamp += 1000;
    engine->addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -520.0 );
    engine->evaluateConditions( timestamp );
}
#endif

TEST_F( CollectionInspectionEngineDoubleTest, EndlessCondition )
{
    CollectionInspectionEngine engine( nullptr );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    ExpressionNode endless;

    endless.nodeType = ExpressionNodeType::OPERATOR_LOGICAL_AND;
    endless.left = &endless;
    endless.right = &endless;

    TimePoint timestamp = { 160000000, 100 };

    // Condition is static by default
    collectionSchemes->conditions[0].condition = &endless;
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );

    uint32_t waitTimeMs = 0;
    EXPECT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TooBigForSignalBuffer )
{
    CollectionInspectionEngine engine( nullptr );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 3072;
    s1.sampleBufferSize =
        500000000; // this number of samples should exceed the maximum buffer size defined in MAX_SAMPLE_MEMORY
    s1.minimumSampleIntervalMs = 5;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static be default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID,
                                 DEFAULT_FETCH_REQUEST_ID,
                                 timestamp,
                                 timestamp.monotonicTimeMs,
                                 0.1 ); // All signal come at the same timestamp
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 1000, timestamp.monotonicTimeMs + 1000, 0.2 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 2000, timestamp.monotonicTimeMs + 2000, 0.3 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 3000 ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 5000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 0 );
}

TEST_F( CollectionInspectionEngineDoubleTest, TooBigForSignalBufferOverflow )
{
    CollectionInspectionEngine engine( nullptr );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 536870912;
    s1.minimumSampleIntervalMs = 5;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    collectionSchemes->conditions[0].minimumPublishIntervalMs = 500;
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 1000, timestamp.monotonicTimeMs + 1000, 0.2 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 2000, timestamp.monotonicTimeMs + 2000, 0.3 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 3000 ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 4000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 0 );
}

TEST_F( CollectionInspectionEngineDoubleTest, SignalBufferErasedAfterNewConditions )
{
    CollectionInspectionEngine engine( nullptr );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 1, timestamp.monotonicTimeMs + 1, 0.2 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 2, timestamp.monotonicTimeMs + 2, 0.3 );

    // This call will flush the signal history buffer even when
    // exactly the same conditions are handed over.
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp + 3 );

    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 3, timestamp.monotonicTimeMs + 3, 0.4 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 3 ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 3, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 0.4 );
}

TEST_F( CollectionInspectionEngineDoubleTest, CollectBurstWithoutSubsampling )
{
    CollectionInspectionEngine engine( nullptr );
    // minimumSampleIntervalMs=0 means no subsampling
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // All signal come at the same timestamp
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.2 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.3 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 0.3 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.2 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );
}

TEST_F( CollectionInspectionEngineDoubleTest, IllegalSignalID )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 0xFFFFFFFF;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );
}

TEST_F( CollectionInspectionEngineDoubleTest, IllegalSampleSize )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 0; // Not allowed
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );
}

TEST_F( CollectionInspectionEngineDoubleTest, ZeroSignalsOnlyDTCCollection )
{
    CollectionInspectionEngine engine( nullptr );
    collectionSchemes->conditions[0].includeActiveDtcs = true;
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    DTCInfo dtcInfo;
    dtcInfo.mDTCCodes.emplace_back( "B1217" );
    dtcInfo.mSID = SID::STORED_DTC;
    dtcInfo.receiveTime = timestamp.systemTimeMs;
    engine.setActiveDTCs( dtcInfo );
    timestamp += 1000;
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    EXPECT_EQ( collectedData->mDTCInfo.mDTCCodes[0], "B1217" );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultipleSubsamplingOfSameSignalUsedInConditions )
{
    CollectionInspectionEngine engine( nullptr );

    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 5678;
    s3.sampleBufferSize = 150;
    s3.minimumSampleIntervalMs = 20;
    s3.fixedWindowPeriod = 300;
    s3.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s3 );
    InspectionMatrixSignalCollectionInfo s5{};
    s5.signalID = 1111;
    s5.sampleBufferSize = 10;
    s5.minimumSampleIntervalMs = 20;
    s5.fixedWindowPeriod = 300;
    s5.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s5 );
    // Condition contains signal
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition =
        getTwoSignalsBiggerCondition( s1.signalID, 150.0, s3.signalID, 155.0 ).get();
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = s1.signalID;
    s2.sampleBufferSize = 200;
    s2.minimumSampleIntervalMs = 50; // Different subsampling then signal in other condition
    s2.fixedWindowPeriod = 150;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    InspectionMatrixSignalCollectionInfo s4{};
    s4.signalID = s3.signalID;
    s4.sampleBufferSize = 130;
    s4.minimumSampleIntervalMs = 60;
    s4.fixedWindowPeriod = 250;
    s4.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s4 );
    InspectionMatrixSignalCollectionInfo s6{};
    s6.signalID = 2222;
    s6.sampleBufferSize = 180;
    s6.minimumSampleIntervalMs = 50;
    s6.fixedWindowPeriod = 250;
    s6.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s6 );
    // Condition contains signal
    collectionSchemes->conditions[1].isStaticCondition = false;
    collectionSchemes->conditions[1].condition =
        getTwoSignalsBiggerCondition( s2.signalID, 165.0, s4.signalID, 170.0 ).get();
    collectionSchemes->conditions[1].minimumPublishIntervalMs = 10000;

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    for ( unsigned int i = 0; i < 100; i++ )
    {
        engine.addNewSignal<double>(
            s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + i * 10, timestamp.monotonicTimeMs + i * 10, i * 2 );
        engine.addNewSignal<double>(
            s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + i * 10, timestamp.monotonicTimeMs + i * 10, i * 2 + 1 );
        engine.addNewSignal<double>(
            s5.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + i * 10, timestamp.monotonicTimeMs + i * 10, 55555 );
        engine.addNewSignal<double>(
            s6.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + i * 10, timestamp.monotonicTimeMs + i * 10, 77777 );
    }

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );

    using CollectionType = std::shared_ptr<const TriggeredCollectionSchemeData>;
    uint32_t waitTimeMs = 0;
    std::vector<CollectionType> collectedDataList;
    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData =
        engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData;
    while ( collectedData != nullptr )
    {
        collectedDataList.push_back( collectedData );
        collectedData = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData;
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
    CollectionInspectionEngine engine( nullptr );

    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 500;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    InspectionMatrixSignalCollectionInfo s5{};
    s5.signalID = 1111;
    s5.sampleBufferSize = 500;
    s5.minimumSampleIntervalMs = 20;
    s5.fixedWindowPeriod = 0;
    s5.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s5 );
    // Condition contains signal
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getLastAvgWindowBiggerCondition( s1.signalID, 150.0 ).get();
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = s1.signalID;
    s2.sampleBufferSize = 500;
    s2.minimumSampleIntervalMs = 50; // Different subsampling then signal in other condition
    s2.fixedWindowPeriod = 200;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    InspectionMatrixSignalCollectionInfo s6{};
    s6.signalID = 2222;
    s6.sampleBufferSize = 500;
    s6.minimumSampleIntervalMs = 50;
    s6.fixedWindowPeriod = 250;
    s6.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s6 );
    // Condition contains signal
    collectionSchemes->conditions[1].isStaticCondition = false;
    collectionSchemes->conditions[1].condition = getLastAvgWindowBiggerCondition( s2.signalID, 150.0 ).get();
    collectionSchemes->conditions[1].minimumPublishIntervalMs = 10000;

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    for ( unsigned int i = 0; i < 300; i++ )
    {
        engine.addNewSignal<double>( s1.signalID,
                                     DEFAULT_FETCH_REQUEST_ID,
                                     timestamp + 10000 + i * 10,
                                     timestamp.monotonicTimeMs + 10000 + i * 10,
                                     i * 2 );
        engine.addNewSignal<double>( s5.signalID,
                                     DEFAULT_FETCH_REQUEST_ID,
                                     timestamp + 10000 + i * 10,
                                     timestamp.monotonicTimeMs + 10000 + i * 10,
                                     55555 );
        engine.addNewSignal<double>( s6.signalID,
                                     DEFAULT_FETCH_REQUEST_ID,
                                     timestamp + 10000 + i * 10,
                                     timestamp.monotonicTimeMs + 10000 + i * 10,
                                     77777 );
    }

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );

    using CollectionType = std::shared_ptr<const TriggeredCollectionSchemeData>;
    uint32_t waitTimeMs = 0;
    std::vector<CollectionType> collectedDataList;
    std::shared_ptr<const TriggeredCollectionSchemeData> collectedData =
        engine.collectNextDataToSend( timestamp + 10000 + 100 * 10, waitTimeMs ).triggeredCollectionSchemeData;
    while ( collectedData != nullptr )
    {
        collectedDataList.push_back( collectedData );
        collectedData =
            engine.collectNextDataToSend( timestamp + 10000 + 300 * 10, waitTimeMs ).triggeredCollectionSchemeData;
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
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    // minimumSampleIntervalMs=10 means faster signals are dropped
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 1, timestamp.monotonicTimeMs + 1, 0.2 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 9, timestamp.monotonicTimeMs + 9, 0.3 );
    // As subsampling is 10 this value should be sampled again
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 10, timestamp.monotonicTimeMs + 10, 0.4 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 40, timestamp.monotonicTimeMs + 40, 0.5 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );

    EXPECT_EQ( collectedData->signals[0].value.value.doubleVal, 0.5 );
    EXPECT_EQ( collectedData->signals[1].value.value.doubleVal, 0.4 );
    EXPECT_EQ( collectedData->signals[2].value.value.doubleVal, 0.1 );
}

// Only valid when sendDataOnlyOncePerCondition is defined true
TEST_F( CollectionInspectionEngineDoubleTest, SendOutEverySignalOnlyOnce )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    // if this is not 0 the samples that come too late will be dropped
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;

    auto res1 = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( res1, nullptr );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );
    auto res2 = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( res2, nullptr );
    EXPECT_EQ( res2->signals.size(), 1 );
    // Very old element in queue gets pushed
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20000 ) );
    auto res3 = engine.collectNextDataToSend( timestamp + 20000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( res3, nullptr );
    EXPECT_EQ( res3->signals.size(), 1 );
}

TYPED_TEST( CollectionInspectionEngineTest, HeartbeatInterval )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = getSignalType<TypeParam>();
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    this->collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    // Condition is static by default
    this->collectionSchemes->conditions[0].condition = this->getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    engine.addNewSignal<TypeParam>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 11 );
    engine.evaluateConditions( timestamp );
    uint32_t waitTimeMs = 0;
    // it should not trigger because less than 10 seconds passed
    EXPECT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<TypeParam>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 10000, timestamp.monotonicTimeMs + 10000, 11 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );

    // Triggers after 10s
    EXPECT_NE( engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<TypeParam>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 15000, timestamp.monotonicTimeMs + 15000, 11 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp + 15000 ) );

    // Not triggers after 15s
    EXPECT_EQ( engine.collectNextDataToSend( timestamp + 15000, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<TypeParam>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 20000, timestamp.monotonicTimeMs + 20000, 11 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20000 ) );

    // Triggers after 20s
    EXPECT_NE( engine.collectNextDataToSend( timestamp + 20000, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
TEST_F( CollectionInspectionEngineDoubleTest, RawBufferHandleUsageHints )
{
    RawData::BufferManager rawDataBufferManager( RawData::BufferManagerConfig::create().get() );
    CollectionInspectionEngine engine( &rawDataBufferManager );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 10;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::COMPLEX_SIGNAL;
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );

    RawData::SignalUpdateConfig signalConfig;
    signalConfig.typeId = s1.signalID;

    rawDataBufferManager.updateConfig( { { signalConfig.typeId, signalConfig } } );

    this->collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000000;
    // Condition is static by default
    this->collectionSchemes->conditions[0].condition = this->getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    for ( unsigned int i = 0; i < 10; i++ )
    {
        timestamp++;
        uint8_t dummyData[] = { 0xDE, 0xAD };
        auto handle = rawDataBufferManager.push( dummyData, sizeof( dummyData ), timestamp.systemTimeMs, s1.signalID );
        engine.addNewSignal<RawData::BufferHandle>(
            s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, handle );
    }
    auto statistics = rawDataBufferManager.getStatistics( s1.signalID );
    EXPECT_EQ( statistics.numOfSamplesCurrentlyInMemory, 10 );
    EXPECT_EQ( statistics.overallNumOfSamplesReceived, 10 );

    for ( unsigned int i = 0; i < 10; i++ )
    {
        timestamp++;
        uint8_t dummyData[] = { 0xDE, 0xAD };
        auto handle = rawDataBufferManager.push( dummyData, sizeof( dummyData ), timestamp.systemTimeMs, s1.signalID );
        engine.addNewSignal<RawData::BufferHandle>(
            s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, handle );
    }
    auto statistics2 = rawDataBufferManager.getStatistics( s1.signalID );
    EXPECT_EQ( statistics2.numOfSamplesCurrentlyInMemory, 10 ); //
    EXPECT_EQ( statistics2.overallNumOfSamplesReceived, 20 );
}

TEST_F( CollectionInspectionEngineDoubleTest, HearbeatIntervalWithVisionSystemData )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 5678;
    s2.sampleBufferSize = 10;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 77777;
    s2.signalType = SignalType::COMPLEX_SIGNAL;
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s2 );

    // Every 10 seconds send data out
    this->collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    // Condition is static be default
    this->collectionSchemes->conditions[0].condition = this->getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 10 );
    engine.addNewSignal<uint32_t>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 9000 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    auto collected = engine.collectNextDataToSend( timestamp, waitTimeMs );
    // it should not trigger because less than 10 seconds passed
    ASSERT_EQ( collected.triggeredCollectionSchemeData, nullptr );
    ASSERT_EQ( collected.triggeredVisionSystemData, nullptr );

    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 10000, timestamp.monotonicTimeMs + 10000, 11 );
    engine.addNewSignal<uint32_t>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 10000, timestamp.monotonicTimeMs + 10000, 9001 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );

    // Triggers after 10s
    collected = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs );
    ASSERT_NE( collected.triggeredCollectionSchemeData, nullptr );
    ASSERT_EQ( collected.triggeredCollectionSchemeData->signals.size(), 2 );
    EXPECT_EQ( collected.triggeredCollectionSchemeData->signals[0].value.value.doubleVal, 11 );
    EXPECT_EQ( collected.triggeredCollectionSchemeData->signals[1].value.value.doubleVal, 10 );

    ASSERT_NE( collected.triggeredVisionSystemData, nullptr );
    ASSERT_EQ( collected.triggeredVisionSystemData->signals.size(), 2 );
    EXPECT_EQ( collected.triggeredVisionSystemData->signals[0].value.value.uint32Val, 9001 );
    EXPECT_EQ( collected.triggeredVisionSystemData->signals[1].value.value.uint32Val, 9000 );
}
#endif

TEST_F( CollectionInspectionEngineDoubleTest, TwoCollectionSchemesWithDifferentNumberOfSamplesToCollect )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 1235;
    s2.sampleBufferSize = 30;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    // Collection Scheme 2 collects the same signal with the same minimumSampleIntervalMs
    // but wants to publish 100 instead of 50 samples
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = s1.signalID;
    s3.sampleBufferSize = 500;
    s3.minimumSampleIntervalMs = s1.minimumSampleIntervalMs;
    s3.fixedWindowPeriod = 77777;
    s3.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s4{};
    s4.signalID = s2.signalID;
    s4.sampleBufferSize = 300;
    s4.minimumSampleIntervalMs = s1.minimumSampleIntervalMs;
    s4.fixedWindowPeriod = 77777;
    s4.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s3 );
    addSignalToCollect( collectionSchemes->conditions[1], s4 );
    // Condition is static by default
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    for ( unsigned int i = 0; i < 10000; i++ )
    {
        timestamp++;
        engine.addNewSignal<double>(
            s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, i * i );
        engine.addNewSignal<double>(
            s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, i + 2 );
    }
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    auto collectedData2 = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    ASSERT_NE( collectedData, nullptr );
    ASSERT_NE( collectedData2, nullptr );

    EXPECT_EQ( collectedData->signals.size(), 80 );
    ASSERT_EQ( collectedData2->signals.size(), 800 );
    // Currently this 800 signals contain the 80 signals again so the same data is sent to cloud
    // twice by different collection schemes, otherwise this would have only 720 samples
}

TEST_F( CollectionInspectionEngineDoubleTest, TwoSignalsInConditionAndOneSignalToCollect )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    s2.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 3;
    s3.sampleBufferSize = 50;
    s3.minimumSampleIntervalMs = 0;
    s3.fixedWindowPeriod = 77777;
    s3.isConditionOnlySignal = false;
    s3.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );
    addSignalToCollect( collectionSchemes->conditions[0], s3 );

    // The condition is (signalID(1)>-100) && (signalID(2)>-500)
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition =
        getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 1000.0 );
    engine.addNewSignal<double>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 2000.0 );
    engine.addNewSignal<double>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 3000.0 );

    // Signals for condition are not available yet so collectionScheme should not trigger
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );

    // Condition only for first signal is fulfilled (-90 > -100) but second not so boolean and is false
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );

    // Condition is fulfilled so it should trigger
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 3 );
    ASSERT_EQ( collectedData->signals[0].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[1].signalID, s3.signalID );
    ASSERT_EQ( collectedData->signals[2].signalID, s3.signalID );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );
}

TEST_F( CollectionInspectionEngineDoubleTest, RisingEdgeTrigger )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    s1.signalType = SignalType::DOUBLE;
    s1.isConditionOnlySignal = false;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );

    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getNotEqualCondition( s1.signalID, s2.signalID ).get();
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 1000.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 2000.0 );

    // Condition evaluates to true but data is not collected (not rising edge)
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.0 );

    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 1000;

    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );

    // Condition is fulfilled so it should trigger (rising edge false->true)
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 2 );
    ASSERT_EQ( collectedData->signals[0].signalID, s1.signalID );
    ASSERT_EQ( collectedData->signals[1].signalID, s1.signalID );
    ASSERT_EQ( collectedData->triggerTime, timestamp.systemTimeMs );
}

// Default is to send data only out once per collectionScheme
TYPED_TEST( CollectionInspectionEngineTest, SendOutEverySignalOnlyOncePerCollectionScheme )
{
    CollectionInspectionEngine engine( nullptr, MIN_FETCH_TRIGGER_MS, nullptr, true );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = getSignalType<TypeParam>();
    this->addSignalToCollect( this->collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    this->collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    // Condition is static by default
    this->collectionSchemes->conditions[0].condition = this->getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    engine.addNewSignal<TypeParam>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 10 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;

    auto res1 = engine.collectNextDataToSend( timestamp + 1, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( res1, nullptr );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );
    auto res2 = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( res2, nullptr );
    EXPECT_EQ( res2->signals.size(), 1 );

    engine.addNewSignal<TypeParam>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 15000, timestamp.monotonicTimeMs + 15000, 10 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20000 ) );
    auto res3 = engine.collectNextDataToSend( timestamp + 20000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( res3, nullptr );
    EXPECT_EQ( res3->signals.size(), 1 );
}

TEST_F( CollectionInspectionEngineDoubleTest, SendOutEverySignalNotOnlyOncePerCollectionScheme )
{
    CollectionInspectionEngine engine( nullptr, 1000, nullptr, false );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 5000;
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;

    auto res1 = engine.collectNextDataToSend( timestamp + 1, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( res1, nullptr );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );
    auto res2 = engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( res2, nullptr );
    EXPECT_EQ( res2->signals.size(), 1 );

    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 15000, timestamp.monotonicTimeMs + 15000, 0.1 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20000 ) );
    auto res3 = engine.collectNextDataToSend( timestamp + 20000, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( res3, nullptr );
    EXPECT_EQ( res3->signals.size(), 2 );
}

TEST_F( CollectionInspectionEngineDoubleTest, MoreCollectionSchemesThanSupported )
{
    CollectionInspectionEngine engine( nullptr, MIN_FETCH_TRIGGER_MS, nullptr, false );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    collectionSchemes->conditions.resize( MAX_NUMBER_OF_ACTIVE_CONDITION + 1 );
    for ( uint32_t i = 0; i < MAX_NUMBER_OF_ACTIVE_CONDITION; i++ )
    {
        // Condition is static be default
        collectionSchemes->conditions[i].condition = getAlwaysTrueCondition().get();
        addSignalToCollect( collectionSchemes->conditions[i], s1 );
    }

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    uint32_t waitTimeMs = 0;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.1 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    engine.collectNextDataToSend( timestamp, waitTimeMs );
    timestamp += 10000;
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    engine.collectNextDataToSend( timestamp, waitTimeMs );
    timestamp += 10000;
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    engine.collectNextDataToSend( timestamp, waitTimeMs );
}

TYPED_TEST( CollectionInspectionEngineTest, CollectWithAfterTime )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.isConditionOnlySignal = true;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 2;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.isConditionOnlySignal = true;
    s2.signalType = SignalType::DOUBLE;
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
    // Condition contains signals
    this->collectionSchemes->conditions[0].isStaticCondition = false;
    this->collectionSchemes->conditions[0].condition =
        this->getTwoSignalsBiggerCondition( s1.signalID, -100.0, s2.signalID, -500.0 ).get();

    // After the collectionScheme triggered signals should be collected for 2 more seconds
    this->collectionSchemes->conditions[0].afterDuration = 2000;

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( this->consCollectionSchemes, timestamp );

    TypeParam val1 = 10;
    engine.addNewSignal<TypeParam>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, val1 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -1000.0 );
    // Condition not fulfilled so should not trigger
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    EXPECT_GE( waitTimeMs, 2000 );

    timestamp += 1000;
    TimePoint timestamp0 = timestamp;
    TypeParam val2 = 20;
    engine.addNewSignal<TypeParam>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, val2 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -480.0 );
    // Condition fulfilled so should trigger but afterTime not over yet
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    EXPECT_EQ( waitTimeMs, 2000 );

    timestamp += 1000;
    TimePoint timestamp1 = timestamp;
    TypeParam val3 = 30;
    engine.addNewSignal<TypeParam>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, val3 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -95.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -485.0 );
    // Condition still fulfilled but already triggered. After time still not over
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    EXPECT_EQ( waitTimeMs, 1000 );

    timestamp += 500;
    TimePoint timestamp2 = timestamp;
    TypeParam val4 = 40;
    engine.addNewSignal<TypeParam>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, val4 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -9000.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -9000.0 );
    // Condition not fulfilled anymore but still waiting for afterTime
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    timestamp += 500;
    TimePoint timestamp3 = timestamp;
    TypeParam val5 = 50;
    engine.addNewSignal<TypeParam>( s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, val5 );
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -9100.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -9100.0 );
    // Condition not fulfilled. After Time is over so data should be sent out
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
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

TEST_F( CollectionInspectionEngineDoubleTest, AvgWindowCondition )
{
    CollectionInspectionEngine engine( nullptr );
    // fixedWindowPeriod means that we collect data over 300 seconds
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 300000;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: LAST_FIXED_WINDOW_AVG(SignalID(1234)) > -50.0
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getLastAvgWindowBiggerCondition( s1.signalID, -50.0 ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    double currentValue = -110.0f;
    double increasePerSample = ( 100.0 ) / static_cast<double>( s1.fixedWindowPeriod );
    uint32_t waitTimeMs = 0;
    // increase value slowly from -110.0 to -10.0 so average is -60
    for ( uint32_t i = 0; i < s1.fixedWindowPeriod; i++ )
    {
        timestamp++;
        currentValue += increasePerSample;
        engine.addNewSignal<double>(
            s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, currentValue );
        ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
        ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    }

    currentValue = -80.0f;
    increasePerSample = ( 100.0 ) / static_cast<double>( s1.fixedWindowPeriod );
    // increase the value slowly from -80 to 20.0f so avg is -30.0
    for ( uint32_t i = 0; i < s1.fixedWindowPeriod - 1; i++ )
    {
        timestamp++;
        currentValue += increasePerSample;
        engine.addNewSignal<double>(
            s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, currentValue );
        ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
        ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    }
    // avg -30 is bigger than -50 so condition fulfilled
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 2 ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp + 2, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, PrevLastAvgWindowCondition )
{
    CollectionInspectionEngine engine( nullptr );
    // fixedWindowPeriod means that we collect data over 300 seconds
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 300000;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: PREV_LAST_FIXED_WINDOW_AVG(SignalID(1234)) > -50.0
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getPrevLastAvgWindowBiggerCondition( s1.signalID, -50.0 ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    uint32_t waitTimeMs = 0;
    // Fill the prev last window with 0.0
    for ( uint32_t i = 0; i < s1.fixedWindowPeriod; i++ )
    {
        timestamp++;
        engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.0 );
        ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
        ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    }
    // No samples arrive for two window2:
    timestamp += s1.fixedWindowPeriod * 2;
    // One more arrives, prev last average is still 0 which is > -50
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -100.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultiWindowCondition )
{
    CollectionInspectionEngine engine( nullptr );
    // fixedWindowPeriod means that we collect data over 300 seconds
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: (((LAST_FIXED_WINDOW_MAX(SignalID(1234)) - PREV_LAST_FIXED_WINDOW_MAX(SignalID(1234)))
    //                + PREV_LAST_FIXED_WINDOW_MAX(SignalID(1234)))
    //               < (LAST_FIXED_WINDOW_MAX(SignalID(1234)) * PREV_LAST_FIXED_WINDOW_MAX(SignalID(1234))))
    //              || (LAST_FIXED_WINDOW_MIN(SignalID(1234)) == PREV_LAST_FIXED_WINDOW_MIN(SignalID(1234)))
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getMultiFixedWindowCondition( s1.signalID ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, -95.0 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 50, timestamp.monotonicTimeMs + 50, 100.0 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 70, timestamp.monotonicTimeMs + 70, 110.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp + 70 ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp + 70, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 100, timestamp.monotonicTimeMs + 100, -205.0 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 150, timestamp.monotonicTimeMs + 150, -300.0 );
    engine.addNewSignal<double>(
        s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 200, timestamp.monotonicTimeMs + 200, +30.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 200 ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp + 200, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TestNotEqualOperator )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 456;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 100;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );

    // function is: !(!(SignalID(123) <= 0.001) && !((SignalID(123) / SignalID(456)) >= 0.5))
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getNotEqualCondition( s1.signalID, s2.signalID ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be false because they are equal
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Expression should be true: because they are not equal
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 50 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Expression should be false because they are equal again
    timestamp++;
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 50.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TestStringOperations )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: ( "string" + "_1" ) == "string_1" && "string_1" != "string_2"
    auto conditionNodePlus = getStringCondition( "string", "_1", ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS ).get();

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeRightString = expressionNodes.back();
    nodeRightString->nodeType = ExpressionNodeType::STRING;
    nodeRightString->stringValue = "string_1";

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeOperationEqual = expressionNodes.back();
    nodeOperationEqual->nodeType = ExpressionNodeType::OPERATOR_EQUAL;
    nodeOperationEqual->left = conditionNodePlus;
    nodeOperationEqual->right = nodeRightString.get();

    auto conditionNodeRight =
        getStringCondition( "string_1", "string_2", ExpressionNodeType::OPERATOR_NOT_EQUAL ).get();

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeOperation = expressionNodes.back();
    nodeOperation->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_AND;
    nodeOperation->left = nodeOperationEqual.get();
    nodeOperation->right = conditionNodeRight;
    // Condition is static be default
    collectionSchemes->conditions[0].condition = nodeOperation.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be true
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TestTypeMismatch )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: "string_1" == 2.0
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeLeft = expressionNodes.back();
    nodeLeft->nodeType = ExpressionNodeType::STRING;
    nodeLeft->stringValue = "string_1";

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeRight = expressionNodes.back();
    nodeRight->nodeType = ExpressionNodeType::FLOAT;
    nodeRight->floatingValue = 2.0;

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeOperation = expressionNodes.back();
    nodeOperation->nodeType = ExpressionNodeType::OPERATOR_EQUAL;
    nodeOperation->left = nodeLeft.get();
    nodeOperation->right = nodeRight.get();
    // Condition is static be default
    collectionSchemes->conditions[0].condition = nodeOperation.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be false because of type mismatch
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TestBoolToDoubleImplicitCast )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // expression is: True + 1.0 == 2.0
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeLeft = expressionNodes.back();
    nodeLeft->nodeType = ExpressionNodeType::BOOLEAN;
    nodeLeft->booleanValue = true;

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeRight = expressionNodes.back();
    nodeRight->nodeType = ExpressionNodeType::FLOAT;
    nodeRight->floatingValue = 1.0;

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeOperation = expressionNodes.back();
    nodeOperation->nodeType = ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS;
    nodeOperation->left = nodeLeft.get();
    nodeOperation->right = nodeRight.get();

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeRight2 = expressionNodes.back();
    nodeRight2->nodeType = ExpressionNodeType::FLOAT;
    nodeRight2->floatingValue = 2.0;

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeOperation2 = expressionNodes.back();
    nodeOperation2->nodeType = ExpressionNodeType::OPERATOR_EQUAL;
    nodeOperation2->left = nodeOperation.get();
    nodeOperation2->right = nodeRight2.get();
    // Condition is static be default
    collectionSchemes->conditions[0].condition = nodeOperation2.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be true
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TestDoubleToBoolImplicitCast )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // expression is: 42.0 && True
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeLeft = expressionNodes.back();
    nodeLeft->nodeType = ExpressionNodeType::FLOAT;
    nodeLeft->floatingValue = 42.0;

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeRight = expressionNodes.back();
    nodeRight->nodeType = ExpressionNodeType::BOOLEAN;
    nodeRight->booleanValue = true;

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto nodeOperation = expressionNodes.back();
    nodeOperation->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_AND;
    nodeOperation->left = nodeLeft.get();
    nodeOperation->right = nodeRight.get();
    // Condition is static be default
    collectionSchemes->conditions[0].condition = nodeOperation.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be true
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TwoSignalsRatioCondition )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 456;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 100;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );

    // function is: !(!(SignalID(123) <= 0.001) && !((SignalID(123) / SignalID(456)) >= 0.5))
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition =
        getTwoSignalsRatioCondition( s1.signalID, 0.001, s2.signalID, 0.5 ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be false: (1.0 <= 0.001) || (1.0 / 100.0) >= 0.5)
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 1.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Expression should be true: (0.001 <= 0.001) || (0.001 / 100.0) >= 0.5)
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 0.001 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Expression should be false: (1.0 <= 0.001) || (1.0 / 100.0) >= 0.5)
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 1.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Expression should be true: (50.0 <= 0.001) || (50.0 / 100.0) >= 0.5)
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 50.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, UnknownExpressionNode )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // function is: (SignalID(123) <= Unknown)
    // Condition is not static
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getUnknownCondition( s1.signalID ).get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Expression should be false: (1.0 <= Unknown)
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 1.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, RequestTooMuchMemorySignals )
{
    CollectionInspectionEngine engine( nullptr );
    // for each of the .sampleBufferSize=1000000 multiple bytes have to be allocated
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 1000000;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 300000;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static by default
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();
    TimePoint timestamp = { 0, 0 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );
}

TEST_F( CollectionInspectionEngineDoubleTest, RandomDataTest )
{
    const int NUMBER_OF_COLLECTION_SCHEMES = 10;
    const int NUMBER_OF_SIGNALS = 10;

    // Keep the seed static so multiple runs are comparable
    std::default_random_engine eng{ static_cast<long unsigned int>( 1620336094 ) };
    std::mt19937 gen{ eng() };
    std::uniform_int_distribution<> uniformDistribution( 0, NUMBER_OF_COLLECTION_SCHEMES - 1 );
    std::normal_distribution<> normalDistribution{ 10, 4 };

    for ( unsigned int i = 0; i < NUMBER_OF_COLLECTION_SCHEMES; i++ )
    {

        ConditionWithCollectedData collectionScheme;
        collectionSchemes->conditions.resize( NUMBER_OF_COLLECTION_SCHEMES );
        // Condition is static by default
        collectionSchemes->conditions[i].condition = getAlwaysTrueCondition().get();
    }

    CollectionInspectionEngine engine( nullptr );
    // for each of the .sampleBufferSize=1000000 multiple bytes have to be allocated
    std::normal_distribution<> fixedWindowSizeGenerator( 300000, 100000 );
    int minWindow = std::numeric_limits<int>::max();
    int maxWindow = std::numeric_limits<int>::min();
    const int MINIMUM_WINDOW_SIZE = 500000; // should be 500000
    int withoutWindow = 0;
    for ( unsigned int i = 0; i < NUMBER_OF_SIGNALS; i++ )
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
        s1.signalType = SignalType::DOUBLE;
        for ( int j = 0; j < normalDistribution( gen ); j++ )
        {
            auto &collectionScheme = collectionSchemes->conditions[uniformDistribution( gen )];
            collectionScheme.signals.push_back( s1 );

            // exactly two signals are added now so add them to condition:
            if ( collectionScheme.signals.size() == 2 )
            {
                // Condition contains signals
                collectionScheme.isStaticCondition = false;
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

    TimePoint START_TIMESTAMP = { 160000000, 100 };
    TimePoint timestamp = START_TIMESTAMP;

    auto originalLogLevel = Aws::IoTFleetWise::gSystemWideLogLevel;
    try
    {
        // Temporarily change the log level since we have too many signals, which would make the test
        // output too noisy with Trace level.
        Aws::IoTFleetWise::gSystemWideLogLevel = Aws::IoTFleetWise::LogLevel::Info;
        engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );
    }
    catch ( ... )
    {
        Aws::IoTFleetWise::gSystemWideLogLevel = originalLogLevel;
        throw;
    }
    Aws::IoTFleetWise::gSystemWideLogLevel = originalLogLevel;

    const int TIME_TO_SIMULATE_IN_MS = 5000;
    const int SIGNALS_PER_MS = 20;
    uint32_t counter = 0;
    uint32_t dataCollected = 0;

    std::default_random_engine eng2{ static_cast<long unsigned int>( 1620337567 ) };
    std::mt19937 gen2{ eng() };
    std::uniform_int_distribution<> signalIDGenerator( 0, NUMBER_OF_SIGNALS - 1 );
    for ( unsigned int i = 0; i < TIME_TO_SIMULATE_IN_MS * SIGNALS_PER_MS; i++ )
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
        engine.addNewSignal<double>( static_cast<SignalID>( signalIDGenerator( gen2 ) ),
                                     DEFAULT_FETCH_REQUEST_ID,
                                     timestamp,
                                     timestamp.monotonicTimeMs,
                                     normalDistribution( gen ) );
        if ( tickIncreased )
        {
            uint32_t waitTimeMs = 0;
            engine.evaluateConditions( timestamp );
            while ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData != nullptr )
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
    CollectionInspectionEngine engine( nullptr );
    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, CollectStringSignal )
{
    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    std::string stringData = "1BDD00";
    InspectionMatrixSignalCollectionInfo s1{};
    CollectedSignal stringSignal;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    RawData::SignalBufferOverrides signalOverrides1;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;

    s1.signalID = 101;
    s1.sampleBufferSize = 10;
    s1.minimumSampleIntervalMs = 100;
    s1.fixedWindowPeriod = 1000;
    s1.signalType = SignalType::STRING;
    collectionSchemes->conditions[0].signals.push_back( s1 );
    // Condition is static be default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    stringSignal.signalID = 101;
    stringSignal.value.type = SignalType::STRING;
    stringSignal.receiveTime = timestamp.systemTimeMs;

    signalUpdateConfig1.typeId = stringSignal.signalID;
    signalUpdateConfig1.interfaceId = "interface1";
    signalUpdateConfig1.messageId = "VEHICLE.DTC_INFO";

    signalOverrides1.interfaceId = signalUpdateConfig1.interfaceId;
    signalOverrides1.messageId = signalUpdateConfig1.messageId;
    signalOverrides1.maxNumOfSamples = 20;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.reservedBytes = 5_MiB;
    signalOverrides1.maxBytes = 100_MiB;

    overridesPerSignal = { signalOverrides1 };

    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::make_optional( (size_t)20 ), boost::none, boost::none, overridesPerSignal );

    updatedSignals = { { signalUpdateConfig1.typeId, signalUpdateConfig1 } };

    RawData::BufferManager rawDataBufferManager( rawDataBufferManagerConfig.get() );
    rawDataBufferManager.updateConfig( updatedSignals );

    CollectionInspectionEngine engine( &rawDataBufferManager );
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    auto handle = rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                                             stringData.length(),
                                             timestamp.systemTimeMs,
                                             stringSignal.signalID );

    stringSignal.value.value.uint32Val = static_cast<uint32_t>( handle );
    engine.addNewSignal<RawData::BufferHandle>( stringSignal.signalID,
                                                DEFAULT_FETCH_REQUEST_ID,
                                                timestamp,
                                                timestamp.monotonicTimeMs,
                                                stringSignal.value.value.uint32Val );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs );

    ASSERT_NE( collectedData.triggeredCollectionSchemeData, nullptr );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    EXPECT_EQ( collectedData.triggeredCollectionSchemeData->signals[0].signalID, stringSignal.signalID );

    auto loanedRawDataFrame = rawDataBufferManager.borrowFrame(
        stringSignal.signalID,
        static_cast<RawData::BufferHandle>(
            collectedData.triggeredCollectionSchemeData->signals[0].value.value.uint32Val ) );
    auto data = loanedRawDataFrame.getData();

    EXPECT_TRUE( 0 == std::memcmp( data, stringData.c_str(), stringData.length() ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, TriggerOnStringSignal )
{
    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    std::string stringData = "1BDD00";

    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;

    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 101;
    s1.sampleBufferSize = 10;
    s1.minimumSampleIntervalMs = 100;
    s1.fixedWindowPeriod = 1000;
    s1.signalType = SignalType::STRING;

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 102;
    s2.sampleBufferSize = 10;
    s2.minimumSampleIntervalMs = 100;
    s2.fixedWindowPeriod = 1000;
    s2.signalType = SignalType::STRING;

    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].signals.push_back( s2 );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getEqualCondition( s1.signalID, s2.signalID ).get();

    CollectedSignal stringSignal1;
    stringSignal1.signalID = 101;
    stringSignal1.value.type = SignalType::STRING;
    stringSignal1.receiveTime = timestamp.systemTimeMs;

    CollectedSignal stringSignal2;
    stringSignal2.signalID = 102;
    stringSignal2.value.type = SignalType::STRING;
    stringSignal2.receiveTime = timestamp.systemTimeMs;

    RawData::SignalUpdateConfig signalUpdateConfig1;
    signalUpdateConfig1.typeId = stringSignal1.signalID;
    signalUpdateConfig1.interfaceId = "interface1";
    signalUpdateConfig1.messageId = "VEHICLE.DTC_INFO_1";

    RawData::SignalUpdateConfig signalUpdateConfig2;
    signalUpdateConfig2.typeId = stringSignal2.signalID;
    signalUpdateConfig2.interfaceId = "interface1";
    signalUpdateConfig2.messageId = "VEHICLE.DTC_INFO_2";

    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.interfaceId = signalUpdateConfig1.interfaceId;
    signalOverrides1.messageId = signalUpdateConfig1.messageId;
    signalOverrides1.maxNumOfSamples = 20;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.reservedBytes = 5_MiB;
    signalOverrides1.maxBytes = 100_MiB;

    RawData::SignalBufferOverrides signalOverrides2;
    signalOverrides2.interfaceId = signalUpdateConfig2.interfaceId;
    signalOverrides2.messageId = signalUpdateConfig2.messageId;
    signalOverrides2.maxNumOfSamples = 20;
    signalOverrides2.maxBytesPerSample = 5_MiB;
    signalOverrides2.reservedBytes = 5_MiB;
    signalOverrides2.maxBytes = 100_MiB;

    overridesPerSignal = { signalOverrides1, signalOverrides2 };

    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::make_optional( (size_t)20 ), boost::none, boost::none, overridesPerSignal );

    updatedSignals = { { signalUpdateConfig1.typeId, signalUpdateConfig1 },
                       { signalUpdateConfig2.typeId, signalUpdateConfig2 } };

    RawData::BufferManager rawDataBufferManager( rawDataBufferManagerConfig.get() );
    rawDataBufferManager.updateConfig( updatedSignals );

    CollectionInspectionEngine engine( &rawDataBufferManager );
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    auto handle1 = rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                                              stringData.length(),
                                              timestamp.systemTimeMs,
                                              stringSignal1.signalID );
    stringSignal1.value.value.uint32Val = static_cast<uint32_t>( handle1 );
    engine.addNewSignal<RawData::BufferHandle>( stringSignal1.signalID,
                                                DEFAULT_FETCH_REQUEST_ID,
                                                timestamp,
                                                timestamp.monotonicTimeMs,
                                                stringSignal1.value.value.uint32Val );

    // Second signal is still not available for the inspection
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );

    auto handle2 = rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                                              stringData.length(),
                                              timestamp.systemTimeMs,
                                              stringSignal2.signalID );
    stringSignal2.value.value.uint32Val = static_cast<uint32_t>( handle2 );
    engine.addNewSignal<RawData::BufferHandle>( stringSignal2.signalID,
                                                DEFAULT_FETCH_REQUEST_ID,
                                                timestamp + 10,
                                                timestamp.monotonicTimeMs + 10,
                                                stringSignal2.value.value.uint32Val );

    // Now condition should trigger
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20 ) );

    auto collectedData = engine.collectNextDataToSend( timestamp + 20, waitTimeMs );

    ASSERT_NE( collectedData.triggeredCollectionSchemeData, nullptr );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 2 );

    auto loanedRawDataFrame = rawDataBufferManager.borrowFrame(
        stringSignal1.signalID,
        static_cast<RawData::BufferHandle>(
            collectedData.triggeredCollectionSchemeData->signals[0].value.value.uint32Val ) );
    auto data = loanedRawDataFrame.getData();

    EXPECT_EQ( collectedData.triggeredCollectionSchemeData->signals[0].signalID, stringSignal1.signalID );
    EXPECT_TRUE( 0 == std::memcmp( data, stringData.c_str(), stringData.length() ) );
    EXPECT_EQ( collectedData.triggeredCollectionSchemeData->signals[1].signalID, stringSignal2.signalID );
    EXPECT_TRUE( 0 == std::memcmp( data, stringData.c_str(), stringData.length() ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, TriggerOnStringSignalBufferHandleDeleted )
{
    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    std::string stringData = "1BDD00";

    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;

    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 101;
    s1.sampleBufferSize = 10;
    s1.minimumSampleIntervalMs = 100;
    s1.fixedWindowPeriod = 1000;
    s1.signalType = SignalType::STRING;

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 102;
    s2.sampleBufferSize = 10;
    s2.minimumSampleIntervalMs = 100;
    s2.fixedWindowPeriod = 1000;
    s2.signalType = SignalType::STRING;

    collectionSchemes->conditions[0].signals.push_back( s1 );
    collectionSchemes->conditions[0].signals.push_back( s2 );
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getEqualCondition( s1.signalID, s2.signalID ).get();

    CollectedSignal stringSignal1;
    stringSignal1.signalID = 101;
    stringSignal1.value.type = SignalType::STRING;
    stringSignal1.receiveTime = timestamp.systemTimeMs;

    CollectedSignal stringSignal2;
    stringSignal2.signalID = 102;
    stringSignal2.value.type = SignalType::STRING;
    stringSignal2.receiveTime = timestamp.systemTimeMs;

    RawData::SignalUpdateConfig signalUpdateConfig1;
    signalUpdateConfig1.typeId = stringSignal1.signalID;
    signalUpdateConfig1.interfaceId = "interface1";
    signalUpdateConfig1.messageId = "VEHICLE.DTC_INFO_1";

    RawData::SignalUpdateConfig signalUpdateConfig2;
    signalUpdateConfig2.typeId = stringSignal2.signalID;
    signalUpdateConfig2.interfaceId = "interface1";
    signalUpdateConfig2.messageId = "VEHICLE.DTC_INFO_2";

    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.interfaceId = signalUpdateConfig1.interfaceId;
    signalOverrides1.messageId = signalUpdateConfig1.messageId;
    // Force deletion of data
    signalOverrides1.maxNumOfSamples = 1;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.reservedBytes = 5_MiB;
    signalOverrides1.maxBytes = 100_MiB;

    RawData::SignalBufferOverrides signalOverrides2;
    signalOverrides2.interfaceId = signalUpdateConfig2.interfaceId;
    signalOverrides2.messageId = signalUpdateConfig2.messageId;
    signalOverrides2.maxNumOfSamples = 20;
    signalOverrides2.maxBytesPerSample = 5_MiB;
    signalOverrides2.reservedBytes = 5_MiB;
    signalOverrides2.maxBytes = 100_MiB;

    overridesPerSignal = { signalOverrides1, signalOverrides2 };

    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::make_optional( (size_t)20 ), boost::none, boost::none, overridesPerSignal );

    updatedSignals = { { signalUpdateConfig1.typeId, signalUpdateConfig1 },
                       { signalUpdateConfig2.typeId, signalUpdateConfig2 } };

    RawData::BufferManager rawDataBufferManager( rawDataBufferManagerConfig.get() );
    rawDataBufferManager.updateConfig( updatedSignals );

    CollectionInspectionEngine engine( &rawDataBufferManager );
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    auto handle1 = rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                                              stringData.length(),
                                              timestamp.systemTimeMs,
                                              stringSignal1.signalID );
    stringSignal1.value.value.uint32Val = static_cast<uint32_t>( handle1 );
    engine.addNewSignal<RawData::BufferHandle>( stringSignal1.signalID,
                                                DEFAULT_FETCH_REQUEST_ID,
                                                timestamp,
                                                timestamp.monotonicTimeMs,
                                                stringSignal1.value.value.uint32Val );

    auto handle2 = rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                                              stringData.length(),
                                              timestamp.systemTimeMs,
                                              stringSignal2.signalID );
    stringSignal2.value.value.uint32Val = static_cast<uint32_t>( handle2 );
    engine.addNewSignal<RawData::BufferHandle>( stringSignal2.signalID,
                                                DEFAULT_FETCH_REQUEST_ID,
                                                timestamp + 10,
                                                timestamp.monotonicTimeMs + 10,
                                                stringSignal2.value.value.uint32Val );

    // This should force handle1 data to be deleted before new handle is accessible by CIE
    rawDataBufferManager.push( reinterpret_cast<const uint8_t *>( stringData.c_str() ),
                               stringData.length(),
                               timestamp.systemTimeMs,
                               stringSignal1.signalID );
    // Buffer handle was deleted
    ASSERT_FALSE( engine.evaluateConditions( timestamp + 20 ) );

    auto collectedData = engine.collectNextDataToSend( timestamp + 20, waitTimeMs );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, MultipleFetchRequests )
{
    // Collection Scheme 1 collects signal with two different fetch request IDs
    // They should be collected in the same signal buffer
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.fetchRequestIDs = { 1, 3 };
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();

    // Collection Scheme 2 collects the same signal with the same minimumSampleIntervalMs
    // but the fetch request id is different
    // This data should land in a different signal buffer
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = s1.signalID;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = s1.minimumSampleIntervalMs;
    s2.fixedWindowPeriod = 77777;
    s2.fetchRequestIDs = { 2 };
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );
    // Condition is static by default
    collectionSchemes->conditions[1].condition = getAlwaysTrueCondition().get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, 1, timestamp, timestamp.monotonicTimeMs, 10 );
    engine.addNewSignal<double>( s2.signalID, 2, timestamp, timestamp.monotonicTimeMs, 20 );
    // Consider sample interval
    engine.addNewSignal<double>( s1.signalID, 3, timestamp + 10, timestamp.monotonicTimeMs + 10, 30 );

    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );

    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp + 50, waitTimeMs ).triggeredCollectionSchemeData;
    auto collectedData2 = engine.collectNextDataToSend( timestamp + 50, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp + 50, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    ASSERT_NE( collectedData, nullptr );
    ASSERT_NE( collectedData2, nullptr );

    // First campaign should collect signal values from one buffer
    // Second campaign should collect signal values from another buffer
    ASSERT_EQ( collectedData->signals.size(), 2 );
    ASSERT_EQ( collectedData2->signals.size(), 1 );

    ASSERT_EQ( collectedData->signals[0].value.value.doubleVal, 30 );
    ASSERT_EQ( collectedData->signals[1].value.value.doubleVal, 10 );
    ASSERT_EQ( collectedData2->signals[0].value.value.doubleVal, 20 );
}

TEST_F( CollectionInspectionEngineDoubleTest, TriggerOnConditionWithCustomFetchLogic )
{
    // Collection Scheme 1 collects signal without custom fetch logic
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysFalseCondition().get();

    // Collection Scheme 2 collects same signal with custom fetch logic
    // and evaluates on it
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 1234;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.fetchRequestIDs = { 1 };
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto equal = expressionNodes.back();
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto signal1 = expressionNodes.back();
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto value = expressionNodes.back();

    signal1->nodeType = ExpressionNodeType::SIGNAL;
    signal1->signalID = s2.signalID;

    value->nodeType = ExpressionNodeType::FLOAT;
    value->floatingValue = 22.0;

    equal->nodeType = ExpressionNodeType::OPERATOR_EQUAL;
    equal->left = signal1.get();
    equal->right = value.get();

    // Condition contains signal
    collectionSchemes->conditions[1].isStaticCondition = false;
    collectionSchemes->conditions[1].condition = equal.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 22.0 );
    engine.addNewSignal<double>( s2.signalID, 1, timestamp, timestamp.monotonicTimeMs, 25.0 );

    uint32_t waitTimeMs = 0;
    // Should evaluate to false. Inspected signal value for campaign 2 is 25.
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<double>( s2.signalID, 1, timestamp + 10, timestamp.monotonicTimeMs + 10, 22.0 );
    // Should evaluate to true. Inspected signal value for campaign 2 is 22.
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20 ) );

    auto collectedData = engine.collectNextDataToSend( timestamp + 50, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp + 60, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    ASSERT_NE( collectedData, nullptr );

    ASSERT_EQ( collectedData->signals.size(), 2 );

    ASSERT_EQ( collectedData->signals[0].value.value.doubleVal, 22.0 );
    ASSERT_EQ( collectedData->signals[1].value.value.doubleVal, 25.0 );
}

TEST_F( CollectionInspectionEngineDoubleTest, TriggerOnConditionWithCustomFetchLogicIsNullFunction )
{
    // Collection Scheme 1 collects signal without custom fetch logic
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysFalseCondition().get();

    // Collection Scheme 2 collects same signal with custom fetch logic
    // and evaluates on it
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 1234;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.fixedWindowPeriod = 77777;
    s2.fetchRequestIDs = { 1 };
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto logicalNot = expressionNodes.back();
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto isNull = expressionNodes.back();
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto signal1 = expressionNodes.back();

    signal1->nodeType = ExpressionNodeType::SIGNAL;
    signal1->signalID = s2.signalID;

    isNull->nodeType = ExpressionNodeType::IS_NULL_FUNCTION;
    isNull->left = signal1.get();

    logicalNot->nodeType = ExpressionNodeType::OPERATOR_LOGICAL_NOT;
    logicalNot->left = isNull.get();

    // Condition contains signal
    collectionSchemes->conditions[1].isStaticCondition = false;
    // Condition contains isNull function
    collectionSchemes->conditions[1].alwaysEvaluateCondition = true;
    collectionSchemes->conditions[1].condition = logicalNot.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 22.0 );

    uint32_t waitTimeMs = 0;
    // Should evaluate to false. There is no signals for campaign 2;
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<double>( s2.signalID, 1, timestamp + 10, timestamp.monotonicTimeMs + 10, 22.0 );
    // Should evaluate to true. There is a signal for campaign 2;
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 20 ) );

    auto collectedData = engine.collectNextDataToSend( timestamp + 50, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp + 60, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 1 );
    ASSERT_EQ( collectedData->signals[0].value.value.doubleVal, 22.0 );

    // Should evaluate to false. There is no unconsumed signal for campaign 2;
    ASSERT_FALSE( engine.evaluateConditions( timestamp + 70 ) );
    engine.addNewSignal<double>( s2.signalID, 1, timestamp + 80, timestamp.monotonicTimeMs + 10, 22.0 );
    // Should evaluate to true. There is an unconsumed signal for campaign 2;
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 90 ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, TriggerOnIsNullFunctionSignalMissing )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.fetchRequestIDs = { 1 };
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s1 );

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto isNull = expressionNodes.back();
    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto signal1 = expressionNodes.back();

    signal1->nodeType = ExpressionNodeType::SIGNAL;
    signal1->signalID = 1; // Non-existing signal

    isNull->nodeType = ExpressionNodeType::IS_NULL_FUNCTION;
    isNull->left = signal1.get();

    // Condition contains signal
    collectionSchemes->conditions[1].isStaticCondition = false;
    // Condition contains isNull function
    collectionSchemes->conditions[1].alwaysEvaluateCondition = true;
    collectionSchemes->conditions[1].condition = isNull.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 22.0 );

    uint32_t waitTimeMs = 0;
    // Should evaluate to false. There is no signal to evaluate on;
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, TriggerOnIsNullFunctionNoSignal )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.fetchRequestIDs = { 1 };
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[1], s1 );

    expressionNodes.push_back( std::make_shared<ExpressionNode>() );
    auto isNull = expressionNodes.back();
    isNull->nodeType = ExpressionNodeType::IS_NULL_FUNCTION;

    // Condition contains signal
    collectionSchemes->conditions[1].isStaticCondition = false;
    // Condition contains isNull function
    collectionSchemes->conditions[1].alwaysEvaluateCondition = true;
    collectionSchemes->conditions[1].condition = isNull.get();

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 22.0 );

    uint32_t waitTimeMs = 0;
    // Should evaluate to false. There is no signal to evaluate on;
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, FetchConfigOnRisingEdgeTest )
{
    auto testFetchQueue = std::make_shared<FetchRequestQueue>( 10, "Test Fetch Queue" );
    CollectionInspectionEngine engine( nullptr, 1000, testFetchQueue );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.fetchRequestIDs = { 1 };
    s1.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Every 10 seconds send data out
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 10000;
    collectionSchemes->conditions[0].triggerOnlyOnRisingEdge = true;
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getOneSignalBiggerCondition( s1.signalID, 0.0 ).get();

    ConditionForFetch fetchCondition1{};
    fetchCondition1.condition = getOneSignalBiggerCondition( s1.signalID, -100.0 ).get();
    fetchCondition1.fetchRequestID = 1;
    fetchCondition1.triggerOnlyOnRisingEdge = true;
    collectionSchemes->conditions[0].fetchConditions = { fetchCondition1 };

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // None of the condition should evaluate to true
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    // Test fetch condition trigger but no campaign condition
    engine.addNewSignal<double>( s1.signalID, 1, timestamp + 10000, timestamp.monotonicTimeMs + 10000, -50.0 );

    // Fetch condition should evaluate to true, upload condition to false
    // New data will be eventually pushed to the signal buffer
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );

    uint32_t waitTimeMs = 0;
    EXPECT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
    ;

    // Simulate fetch logic
    engine.addNewSignal<double>( s1.signalID, 1, timestamp + 11000, timestamp.monotonicTimeMs + 11000, 11 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 11000 ) );

    // Triggers after 10s
    EXPECT_NE( engine.collectNextDataToSend( timestamp + 11000, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // None of the condition should evaluate to true due to rising edge setup
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
}

TEST_F( CollectionInspectionEngineDoubleTest, FetchConfigTriggerOnDifferentSignalTest )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.fixedWindowPeriod = 77777;
    s1.fetchRequestIDs = { 1 };
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 456;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 100;
    s2.isConditionOnlySignal = true;
    s2.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s3{};
    s3.signalID = 789;
    s3.sampleBufferSize = 50;
    s3.minimumSampleIntervalMs = 0;
    s3.fixedWindowPeriod = 100;
    s3.isConditionOnlySignal = true;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );
    addSignalToCollect( collectionSchemes->conditions[0], s3 );

    collectionSchemes->conditions[0].minimumPublishIntervalMs = 100;
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getOneSignalBiggerCondition( s2.signalID, 20.0 ).get();

    ConditionForFetch fetchCondition1{};
    fetchCondition1.condition = getNotEqualCondition( s2.signalID, s3.signalID ).get();
    fetchCondition1.fetchRequestID = 1;
    fetchCondition1.triggerOnlyOnRisingEdge = false;
    collectionSchemes->conditions[0].fetchConditions = { fetchCondition1 };

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Fetch and normal condition should not trigger
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );

    engine.addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 100, timestamp.monotonicTimeMs + 100, 100 );
    // Now condition evaluation would evaluate to true but no signal was yet fetched
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 150 ) );
    uint32_t waitTimeMs = 0;
    // Expect a non-null but an empty object
    auto collectedData = engine.collectNextDataToSend( timestamp + 150, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_NE( collectedData, nullptr );
    ASSERT_EQ( collectedData->signals.size(), 0 );

    engine.addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 200, timestamp.monotonicTimeMs + 200, 11 );
    engine.addNewSignal<double>(
        s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 200, timestamp.monotonicTimeMs + 200, 11 );
    // Fetch condition should still evaluate to false and campaign condition is false now too
    ASSERT_FALSE( engine.evaluateConditions( timestamp + 250 ) );
    EXPECT_EQ( engine.collectNextDataToSend( timestamp + 250, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 300, timestamp.monotonicTimeMs + 300, 22 );
    engine.addNewSignal<double>(
        s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 300, timestamp.monotonicTimeMs + 300, 33 );
    // Fetch condition should evaluate to true
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 350 ) );

    // Simulate fetch logic
    engine.addNewSignal<double>( s1.signalID, 1, timestamp + 10000, timestamp.monotonicTimeMs + 10000, 11 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10000 ) );

    // Triggers after 10s
    EXPECT_NE( engine.collectNextDataToSend( timestamp + 10000, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    engine.addNewSignal<double>(
        s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 10100, timestamp.monotonicTimeMs + 10100, 33 );
    engine.addNewSignal<double>(
        s3.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp + 10100, timestamp.monotonicTimeMs + 10100, 44 );
    // Fetch condition should evaluate to true again
    ASSERT_TRUE( engine.evaluateConditions( timestamp + 10100 ) );
}

// Test to assert that a signal buffer is not allocated for a signal not known to DM
TEST_F( CollectionInspectionEngineDoubleTest, UnknownSignalNoSignalBuffer )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.signalType = SignalType::UNKNOWN;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    // Condition is static by default
    collectionSchemes->conditions[0].condition = getAlwaysTrueCondition().get();
    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 100.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    // Condition will evaluate to true but no data will be collected
    uint32_t waitTimeMs = 0;
    auto collectedData = engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData;
    ASSERT_EQ( collectedData->signals.size(), 0 );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp + 5000, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, UnknownSignalInExpression )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 123;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 10;
    s1.signalType = SignalType::UNKNOWN;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );

    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 456;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 10;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s2 );

    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getNotEqualCondition( s1.signalID, s2.signalID ).get();
    collectionSchemes->conditions[0].minimumPublishIntervalMs = 5000;

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // Condition evaluates to false
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 90.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 90.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Condition still evaluates to false after time increment
    timestamp += 5000;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 110.0 );
    engine.addNewSignal<double>( s2.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 110.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );
}

TEST_F( CollectionInspectionEngineDoubleTest, CustomFunction )
{
    CollectionInspectionEngine engine( nullptr );
    InspectionMatrixSignalCollectionInfo s1{};
    s1.signalID = 1234;
    s1.sampleBufferSize = 50;
    s1.minimumSampleIntervalMs = 0;
    s1.fixedWindowPeriod = 100;
    s1.signalType = SignalType::DOUBLE;
    InspectionMatrixSignalCollectionInfo s2{};
    s2.signalID = 5678;
    s2.sampleBufferSize = 50;
    s2.minimumSampleIntervalMs = 0;
    s2.fixedWindowPeriod = 100;
    s2.signalType = SignalType::DOUBLE;
    addSignalToCollect( collectionSchemes->conditions[0], s1 );
    addSignalToCollect( collectionSchemes->conditions[0], s2 );
    addSignalToCollect( collectionSchemes->conditions[1], s1 );
    addSignalToCollect( collectionSchemes->conditions[1], s2 );

    // function is: custom_function(name, signal)
    // Condition contains signals
    collectionSchemes->conditions[0].isStaticCondition = false;
    collectionSchemes->conditions[0].condition = getCustomFunctionCondition( s1.signalID, "ABC" ).get();
    collectionSchemes->conditions[0].alwaysEvaluateCondition = true;
    // Not implemented function:
    collectionSchemes->conditions[1].isStaticCondition = false;
    collectionSchemes->conditions[1].condition = getCustomFunctionCondition( s1.signalID, "DEF" ).get();
    collectionSchemes->conditions[1].alwaysEvaluateCondition = true;
    MockFunction<CustomFunctionInvokeResult( CustomFunctionInvocationID, const std::vector<InspectionValue> & )> invoke;
    EXPECT_CALL( invoke, Call( _, _ ) )
        .Times( 4 )
        .WillOnce( Invoke( []( CustomFunctionInvocationID invocationId,
                               const std::vector<InspectionValue> &args ) -> CustomFunctionInvokeResult {
            EXPECT_EQ( invocationId, 0 );
            EXPECT_EQ( args.size(), 1 );
            EXPECT_TRUE( args[0].isUndefined() );
            return ExpressionErrorCode::SUCCESSFUL;
        } ) )
        .WillRepeatedly( Invoke( [s1]( CustomFunctionInvocationID invocationId,
                                       const std::vector<InspectionValue> &args ) -> CustomFunctionInvokeResult {
            EXPECT_EQ( invocationId, 0 );
            EXPECT_EQ( args.size(), 1 );
            EXPECT_TRUE( args[0].isBoolOrDouble() );
            EXPECT_EQ( args[0].signalID, s1.signalID );
            if ( args[0].asDouble() >= 300.0 )
            {
                return ExpressionErrorCode::TYPE_MISMATCH;
            }
            return { ExpressionErrorCode::SUCCESSFUL, args[0].asDouble() > 200.0 };
        } ) );
    MockFunction<void( const std::unordered_set<SignalID> &, Timestamp, CollectionInspectionEngineOutput & )>
        conditionEnd;
    EXPECT_CALL( conditionEnd, Call( _, _, _ ) )
        .Times( 8 )
        .WillRepeatedly( Invoke( [s1, s2]( const std::unordered_set<SignalID> &collectedSignalIds,
                                           Timestamp timestamp,
                                           CollectionInspectionEngineOutput &collectedData ) {
            EXPECT_EQ( collectedSignalIds.size(), 2 );
            EXPECT_NE( collectedSignalIds.find( s1.signalID ), collectedSignalIds.end() );
            EXPECT_NE( collectedSignalIds.find( s2.signalID ), collectedSignalIds.end() );
            if ( !collectedData.triggeredCollectionSchemeData )
            {
                return;
            }
            collectedData.triggeredCollectionSchemeData->signals.emplace_back(
                s2.signalID, timestamp, 7890.0, SignalType::DOUBLE );
        } ) );
    MockFunction<void( CustomFunctionInvocationID )> cleanup;
    EXPECT_CALL( cleanup, Call( _ ) ).Times( 1 );
    engine.registerCustomFunction(
        "ABC",
        CustomFunctionCallbacks{ invoke.AsStdFunction(), conditionEnd.AsStdFunction(), cleanup.AsStdFunction() } );

    TimePoint timestamp = { 160000000, 100 };
    engine.onChangeInspectionMatrix( consCollectionSchemes, timestamp );

    // No signal data yet available:
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    uint32_t waitTimeMs = 0;
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // First value, not triggered:
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 123.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Trigger:
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 250.0 );
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    ASSERT_NE( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Out of range:
    timestamp++;
    engine.addNewSignal<double>( s1.signalID, DEFAULT_FETCH_REQUEST_ID, timestamp, timestamp.monotonicTimeMs, 500.0 );
    ASSERT_FALSE( engine.evaluateConditions( timestamp ) );
    ASSERT_EQ( engine.collectNextDataToSend( timestamp, waitTimeMs ).triggeredCollectionSchemeData, nullptr );

    // Cleanup
    timestamp++;
    engine.onChangeInspectionMatrix( std::make_shared<InspectionMatrix>(), timestamp );
}

} // namespace IoTFleetWise
} // namespace Aws
