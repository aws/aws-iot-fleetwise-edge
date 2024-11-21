// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionMultiRisingEdgeTrigger.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "NamedSignalDataSource.h"
#include "QueueTypes.h"
#include "RawDataBufferManagerSpy.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include <boost/optional/optional.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::NiceMock;

class CustomFunctionMultiRisingEdgeTriggerTest : public ::testing::Test
{
public:
    CustomFunctionMultiRisingEdgeTriggerTest()
        : mDictionary( std::make_shared<CustomDecoderDictionary>() )
        , mSignalBuffer( std::make_shared<SignalBuffer>( 100, "Signal Buffer" ) )
        , mSignalBufferDistributor( std::make_shared<SignalBufferDistributor>() )
        , mNamedSignalDataSource( std::make_shared<NamedSignalDataSource>( "NAMED_SIGNAL", mSignalBufferDistributor ) )
        , mRawBufferManagerSpy( std::make_shared<NiceMock<Testing::RawDataBufferManagerSpy>>(
              RawData::BufferManagerConfig::create().get() ) )
    {
        mDictionary->customDecoderMethod["NAMED_SIGNAL"]["Vehicle.MultiRisingEdgeTrigger"] =
            CustomSignalDecoderFormat{ "NAMED_SIGNAL", "Vehicle.MultiRisingEdgeTrigger", 1, SignalType::STRING };
        mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
        mSignalBufferDistributor->registerQueue( mSignalBuffer );
        mRawBufferManagerSpy->updateConfig( { { 1, { 1, "", "" } } } );
    }
    void
    SetUp() override
    {
    }
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
    std::shared_ptr<SignalBuffer> mSignalBuffer;
    std::shared_ptr<SignalBufferDistributor> mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<NiceMock<Testing::RawDataBufferManagerSpy>> mRawBufferManagerSpy;
};

TEST_F( CustomFunctionMultiRisingEdgeTriggerTest, wrongArgs )
{
    CustomFunctionMultiRisingEdgeTrigger customFunc( mNamedSignalDataSource, mRawBufferManagerSpy );
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    // Try no args
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Try bad datatype:
    args.resize( 4 );
    args[0] = -1;
    args[1] = false;
    args[2] = "def";
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Successfully add an initial value:
    args[0] = "abc";
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Change the datatype:
    args[0] = -1;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Change the number of args:
    args[0] = "abc";
    args.resize( 2 );
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    customFunc.cleanup( 1 );
}

TEST_F( CustomFunctionMultiRisingEdgeTriggerTest, multiTrigger )
{
    CustomFunctionMultiRisingEdgeTrigger customFunc( mNamedSignalDataSource, mRawBufferManagerSpy );
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    // Undefined initial value:
    args.resize( 4 );
    args[0] = "abc";
    args[2] = "def";
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Initial value:
    args.resize( 4 );
    args[1] = false;
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // First rising edge:
    args[1] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    auto &signal = collectedData.triggeredCollectionSchemeData->signals[0];
    auto loanedFrame = mRawBufferManagerSpy->borrowFrame( signal.signalID, signal.value.value.uint32Val );
    ASSERT_FALSE( loanedFrame.isNull() );
    std::string collectedStringVal;
    collectedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( collectedStringVal, "[\"abc\"]" );

    // Falling edge:
    args[3] = false;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Second rising edge:
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    signal = collectedData.triggeredCollectionSchemeData->signals[0];
    loanedFrame = mRawBufferManagerSpy->borrowFrame( signal.signalID, signal.value.value.uint32Val );
    ASSERT_FALSE( loanedFrame.isNull() );
    collectedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( collectedStringVal, "[\"def\"]" );

    // Falling edge on both
    args[1] = false;
    args[3] = false;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Rising edge on both:
    args[1] = true;
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    signal = collectedData.triggeredCollectionSchemeData->signals[0];
    loanedFrame = mRawBufferManagerSpy->borrowFrame( signal.signalID, signal.value.value.uint32Val );
    ASSERT_FALSE( loanedFrame.isNull() );
    collectedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( collectedStringVal, "[\"abc\",\"def\"]" );

    // Falling edge on both:
    args[1] = false;
    args[3] = false;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Signal not collected:
    collectedSignalIds.erase( 1 );

    // Rising edge:
    args[1] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Collect signal again:
    collectedSignalIds.emplace( 1 );
    // Remove raw buffer manager config:
    mRawBufferManagerSpy->updateConfig( {} );

    // Rising edge:
    args[1] = false;
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Add raw buffer config again:
    mRawBufferManagerSpy->updateConfig( { { 1, { 1, "", "" } } } );
    // Change of decoder manifest removes custom decoders
    mDictionary->customDecoderMethod.clear();
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    // Rising edge:
    args[1] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    customFunc.cleanup( 1 );
}

TEST_F( CustomFunctionMultiRisingEdgeTriggerTest, noNamedSignalDataSource )
{
    CustomFunctionMultiRisingEdgeTrigger customFunc( nullptr, mRawBufferManagerSpy );
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    // Successfully add an initial value:
    args.resize( 4 );
    args[0] = "abc";
    args[1] = false;
    args[2] = "def";
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // First rising edge:
    args[1] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    customFunc.cleanup( 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
