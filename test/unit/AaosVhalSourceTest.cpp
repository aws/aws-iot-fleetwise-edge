// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AaosVhalSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <array>
#include <boost/lockfree/queue.hpp>
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
        std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> frameMap;
        CANMessageDecoderMethod decoderMethod;
        decoderMethod.collectType = CANMessageCollectType::DECODE;
        decoderMethod.format.mMessageID = 12345;
        CANSignalFormat sig1;
        CANSignalFormat sig2;
        sig1.mOffset = 0x0207;
        sig1.mFirstBitPosition = 0xAA;
        sig1.mSizeInBits = 0x55;
        sig1.mSignalID = 0x1234;
        sig2.mOffset = 0x0209;
        sig2.mFirstBitPosition = 0x55;
        sig2.mSizeInBits = 0xAA;
        sig2.mSignalID = 0x5678;
        decoderMethod.format.mSignals.push_back( sig1 );
        decoderMethod.format.mSignals.push_back( sig2 );
        frameMap[1] = decoderMethod;
        mDictionary = std::make_shared<CANDecoderDictionary>();
        mDictionary->canMessageDecoderMethod[1] = frameMap;
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<CANDecoderDictionary> mDictionary;
};

TEST_F( AaosVhalSourceTest, testDecoding ) // NOLINT
{
    SignalBufferPtr signalBufferPtr = std::make_shared<SignalBuffer>( 100 );
    AaosVhalSource vhalSource( signalBufferPtr );
    ASSERT_FALSE( vhalSource.init( INVALID_CAN_SOURCE_NUMERIC_ID, 1 ) );
    ASSERT_TRUE( vhalSource.init( 1, 1 ) );
    vhalSource.start();
    CollectedSignal firstSignal;
    CollectedSignal secondSignal;
    DELAY_ASSERT_FALSE( signalBufferPtr->pop( firstSignal ) );
    vhalSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    DELAY_ASSERT_FALSE( signalBufferPtr->pop( firstSignal ) );
    auto propInfo = vhalSource.getVehiclePropertyInfo();
    ASSERT_EQ( 2, propInfo.size() );
    auto sig1Info = std::array<uint32_t, 4>{ 0x0207, 0xAA, 0x55, 0x1234 };
    ASSERT_EQ( sig1Info, propInfo[0] );
    auto sig2Info = std::array<uint32_t, 4>{ 0x0209, 0x55, 0xAA, 0x5678 };
    ASSERT_EQ( sig2Info, propInfo[1] );
    vhalSource.setVehicleProperty( 0x1234, 52.5761 );
    vhalSource.setVehicleProperty( 0x5678, 12.5761 );

    WAIT_ASSERT_TRUE( signalBufferPtr->pop( firstSignal ) );
    ASSERT_TRUE( signalBufferPtr->pop( secondSignal ) );
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, 12.5761, 0.0001 );

    ASSERT_TRUE( vhalSource.stop() );
}

} // namespace IoTFleetWise
} // namespace Aws
