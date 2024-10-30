// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExternalGpsSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class ExternalGpsSourceTest : public ::testing::Test
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
        sig1.mFirstBitPosition = 0;
        sig1.mSignalID = 0x1234;
        sig2.mFirstBitPosition = 32;
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

// Test if valid gps data
TEST_F( ExternalGpsSourceTest, testDecoding ) // NOLINT
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 100, "Signal Buffer" );
    auto signalBufferDistributor = std::make_shared<SignalBufferDistributor>();
    signalBufferDistributor->registerQueue( signalBuffer );
    ExternalGpsSource gpsSource( signalBufferDistributor );
    ASSERT_FALSE( gpsSource.init( INVALID_CAN_SOURCE_NUMERIC_ID, 1, 0, 32 ) );
    ASSERT_TRUE( gpsSource.init( 1, 1, 0, 32 ) );
    gpsSource.start();
    gpsSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );

    CollectedDataFrame collectedDataFrame;
    DELAY_ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );

    gpsSource.setLocation( 360, 360 ); // Invalid
    gpsSource.setLocation( 52.5761, 12.5761 );

    WAIT_ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );

    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    auto secondSignal = collectedDataFrame.mCollectedSignals[1];

    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, 12.5761, 0.0001 );

    ASSERT_TRUE( gpsSource.init( 1, 1, 123, 456 ) ); // Invalid start bits
    gpsSource.setLocation( 52.5761, 12.5761 );
    DELAY_ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );

    ASSERT_TRUE( gpsSource.stop() );
}

// Test longitude west
TEST_F( ExternalGpsSourceTest, testWestNegativeLongitude ) // NOLINT
{
    SignalBufferPtr signalBuffer = std::make_shared<SignalBuffer>( 100, "Signal Buffer" );
    auto signalBufferDistributor = std::make_shared<SignalBufferDistributor>();
    signalBufferDistributor->registerQueue( signalBuffer );
    ExternalGpsSource gpsSource( signalBufferDistributor );
    gpsSource.init( 1, 1, 0, 32 );
    gpsSource.start();
    CollectedDataFrame collectedDataFrame;
    DELAY_ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
    gpsSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    DELAY_ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
    gpsSource.setLocation( 52.5761, -12.5761 );

    WAIT_ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    auto secondSignal = collectedDataFrame.mCollectedSignals[1];
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, -12.5761, 0.0001 ); // negative number

    ASSERT_TRUE( gpsSource.stop() );
}

} // namespace IoTFleetWise
} // namespace Aws
