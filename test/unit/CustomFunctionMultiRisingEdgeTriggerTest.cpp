// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CustomFunctionMultiRisingEdgeTrigger.h"
#include "RawDataBufferManagerSpy.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
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
        , mNamedSignalDataSource( std::make_shared<NamedSignalDataSource>( "NAMED_SIGNAL", mSignalBufferDistributor ) )
        , mRawDataBufferManagerSpy( RawData::BufferManagerConfig::create().get() )
    {
        mDictionary->customDecoderMethod["NAMED_SIGNAL"]["Vehicle.MultiRisingEdgeTrigger"] =
            CustomSignalDecoderFormat{ "NAMED_SIGNAL", "Vehicle.MultiRisingEdgeTrigger", 1, SignalType::STRING };
        mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
        mSignalBufferDistributor.registerQueue( mSignalBuffer );
        mRawDataBufferManagerSpy.updateConfig( { { 1, { 1, "", "" } } } );
    }
    void
    SetUp() override
    {
    }
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
    std::shared_ptr<SignalBuffer> mSignalBuffer;
    SignalBufferDistributor mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    NiceMock<Testing::RawDataBufferManagerSpy> mRawDataBufferManagerSpy;
};

TEST_F( CustomFunctionMultiRisingEdgeTriggerTest, wrongArgs )
{
    CustomFunctionMultiRisingEdgeTrigger customFunc( mNamedSignalDataSource, &mRawDataBufferManagerSpy );
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
    CustomFunctionMultiRisingEdgeTrigger customFunc( mNamedSignalDataSource, &mRawDataBufferManagerSpy );
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
    auto loanedFrame = mRawDataBufferManagerSpy.borrowFrame( signal.signalID, signal.value.value.uint32Val );
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
    loanedFrame = mRawDataBufferManagerSpy.borrowFrame( signal.signalID, signal.value.value.uint32Val );
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
    loanedFrame = mRawDataBufferManagerSpy.borrowFrame( signal.signalID, signal.value.value.uint32Val );
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
    mRawDataBufferManagerSpy.updateConfig( {} );

    // Rising edge:
    args[1] = false;
    args[3] = true;
    ASSERT_EQ( customFunc.invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    customFunc.conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Add raw buffer config again:
    mRawDataBufferManagerSpy.updateConfig( { { 1, { 1, "", "" } } } );
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
    CustomFunctionMultiRisingEdgeTrigger customFunc( nullptr, &mRawDataBufferManagerSpy );
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
