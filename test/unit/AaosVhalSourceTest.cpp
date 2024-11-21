// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AaosVhalSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class AaosVhalSourceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        std::unordered_map<CustomSignalDecoder, CustomSignalDecoderFormat> customDecoders;
        customDecoders["0x0207,0xAA,0x55"] =
            CustomSignalDecoderFormat{ "5", "0x0207,0xAA,0x55", 0x1234, SignalType::DOUBLE };
        customDecoders["0x0209,0x55,0xAA"] =
            CustomSignalDecoderFormat{ "5", "0x0209,0x55,0xAA", 0x5678, SignalType::INT8 };
        customDecoders["0x020A,0x77,0xBB"] =
            CustomSignalDecoderFormat{ "5", "0x020A,0x77,0xBB", 0x8888, SignalType::INT64 };
        mDictionary = std::make_shared<CustomDecoderDictionary>();
        mDictionary->customDecoderMethod["AAOS-VHAL"] = customDecoders;
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<CustomDecoderDictionary> mDictionary;
};

TEST_F( AaosVhalSourceTest, testDecoding ) // NOLINT
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 100, "Signal Buffer" );
    auto signalBufferDistributor = std::make_shared<SignalBufferDistributor>();
    signalBufferDistributor->registerQueue( signalBuffer );
    AaosVhalSource vhalSource( "AAOS-VHAL", signalBufferDistributor );

    CollectedDataFrame collectedDataFrame;
    DELAY_ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
    vhalSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    DELAY_ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );

    std::unordered_map<uint32_t, std::array<uint32_t, 4>> propInfoBySignalId;
    for ( auto propInfo : vhalSource.getVehiclePropertyInfo() )
    {
        propInfoBySignalId[propInfo[3]] = propInfo;
    }
    ASSERT_EQ( 3, vhalSource.getVehiclePropertyInfo().size() );
    ASSERT_EQ( 3, propInfoBySignalId.size() );
    auto sig1Info = std::array<uint32_t, 4>{ 0x0207, 0xAA, 0x55, 0x1234 };
    ASSERT_EQ( sig1Info, propInfoBySignalId[0x1234] );
    auto sig2Info = std::array<uint32_t, 4>{ 0x0209, 0x55, 0xAA, 0x5678 };
    ASSERT_EQ( sig2Info, propInfoBySignalId[0x5678] );
    auto sig3Info = std::array<uint32_t, 4>{ 0x020A, 0x77, 0xBB, 0x8888 };
    ASSERT_EQ( sig3Info, propInfoBySignalId[0x8888] );

    vhalSource.setVehicleProperty( 0x1234, DecodedSignalValue( 52.5761, SignalType::DOUBLE ) );
    vhalSource.setVehicleProperty( 0x5678, DecodedSignalValue( 12, SignalType::DOUBLE ) );
    vhalSource.setVehicleProperty( 0x8888, DecodedSignalValue( 123456, SignalType::INT64 ) );

    WAIT_ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    WAIT_ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto secondSignal = collectedDataFrame.mCollectedSignals[0];
    WAIT_ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto thirdSignal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_EQ( thirdSignal.signalID, 0x8888 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_EQ( secondSignal.value.value.int8Val, 12 );
    ASSERT_EQ( thirdSignal.value.value.int64Val, 123456 );
}

} // namespace IoTFleetWise
} // namespace Aws
