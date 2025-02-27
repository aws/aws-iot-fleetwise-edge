// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class NamedSignalDataSourceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        mDictionary = std::make_shared<CustomDecoderDictionary>();
        mDictionary->customDecoderMethod["5"]["Vehicle.MySignal1"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.MySignal1", 1, SignalType::DOUBLE };
        mDictionary->customDecoderMethod["5"]["Vehicle.MySignal2"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.MySignal2", 2, SignalType::DOUBLE };
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<CustomDecoderDictionary> mDictionary;
};

TEST_F( NamedSignalDataSourceTest, testNoDecoderDictionary )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 100, "Signal Buffer" );
    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );
    NamedSignalDataSource namedSignalSource( "5", signalBufferDistributor );

    namedSignalSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.MySignal1" ), INVALID_SIGNAL_ID );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.MySignal2" ), INVALID_SIGNAL_ID );
    namedSignalSource.ingestSignalValue( 0, "Vehicle.MySignal1", DecodedSignalValue( 123, SignalType::DOUBLE ) );
    std::vector<std::pair<std::string, DecodedSignalValue>> values;
    values.push_back( std::make_pair( "Vehicle.MySignal1", DecodedSignalValue( 456, SignalType::DOUBLE ) ) );
    values.push_back( std::make_pair( "Vehicle.MySignal2", DecodedSignalValue( 789, SignalType::DOUBLE ) ) );
    namedSignalSource.ingestMultipleSignalValues( 1, values );
    CollectedDataFrame collectedDataFrame;
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
    namedSignalSource.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::CUSTOM_DECODING );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.MySignal1" ), INVALID_SIGNAL_ID );
    namedSignalSource.ingestSignalValue( 0, "Vehicle.MySignal1", DecodedSignalValue( 123, SignalType::DOUBLE ) );
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

TEST_F( NamedSignalDataSourceTest, wrongInterface )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 100, "Signal Buffer" );
    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );
    NamedSignalDataSource namedSignalSource( "2", signalBufferDistributor ); // Unknown interface
    namedSignalSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.MySignal1" ), INVALID_SIGNAL_ID );
    namedSignalSource.ingestSignalValue( 0, "Vehicle.MySignal1", DecodedSignalValue( 123, SignalType::DOUBLE ) );
    std::vector<std::pair<std::string, DecodedSignalValue>> values;
    values.push_back( std::make_pair( "Vehicle.MySignal1", DecodedSignalValue( 456, SignalType::DOUBLE ) ) );
    values.push_back( std::make_pair( "Vehicle.MySignal2", DecodedSignalValue( 789, SignalType::DOUBLE ) ) );
    namedSignalSource.ingestMultipleSignalValues( 1, values );
    CollectedDataFrame collectedDataFrame;
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

TEST_F( NamedSignalDataSourceTest, testDecoding )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 2, "Signal Buffer" );
    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );
    NamedSignalDataSource namedSignalSource( "5", signalBufferDistributor );

    namedSignalSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.SomeOtherSignal" ), INVALID_SIGNAL_ID );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.MySignal1" ), 1 );
    ASSERT_EQ( namedSignalSource.getNamedSignalID( "Vehicle.MySignal2" ), 2 );
    namedSignalSource.ingestSignalValue(
        0, "Vehicle.SomeOtherSignal", DecodedSignalValue( 123, SignalType::DOUBLE ) ); // Unknown signal
    CollectedDataFrame collectedDataFrame;
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );

    namedSignalSource.ingestSignalValue( 0, "Vehicle.MySignal1", DecodedSignalValue( 123, SignalType::DOUBLE ) );
    std::vector<std::pair<std::string, DecodedSignalValue>> values;
    values.push_back( std::make_pair( "Vehicle.MySignal1", DecodedSignalValue( 456, SignalType::DOUBLE ) ) );
    values.push_back( std::make_pair( "Vehicle.MySignal2", DecodedSignalValue( 789, SignalType::DOUBLE ) ) );
    values.push_back( std::make_pair( "Vehicle.SomeOtherSignal", DecodedSignalValue( 222, SignalType::DOUBLE ) ) );
    namedSignalSource.ingestMultipleSignalValues( 1, values );
    namedSignalSource.ingestSignalValue(
        0, "Vehicle.MySignal1", DecodedSignalValue( 555, SignalType::DOUBLE ) ); // Queue full
    namedSignalSource.ingestMultipleSignalValues( 1, values );                   // Queue full
    ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals[0].signalID, 1 );
    ASSERT_NEAR( collectedDataFrame.mCollectedSignals[0].value.value.doubleVal, 123, 0.0001 );
    ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 2 );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals[0].signalID, 1 );
    ASSERT_NEAR( collectedDataFrame.mCollectedSignals[0].value.value.doubleVal, 456, 0.0001 );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals[0].receiveTime, 1 );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals[1].signalID, 2 );
    ASSERT_NEAR( collectedDataFrame.mCollectedSignals[1].value.value.doubleVal, 789, 0.0001 );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals[1].receiveTime, 1 );
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

} // namespace IoTFleetWise
} // namespace Aws
