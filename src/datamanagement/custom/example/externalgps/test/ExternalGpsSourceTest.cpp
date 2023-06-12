// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "ExternalGpsSource.h"
#include "WaitUntil.h"
#include <gtest/gtest.h>
#include <thread>

using namespace Aws::IoTFleetWise::DataManagement;

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
    SignalBufferPtr signalBufferPtr = std::make_shared<SignalBuffer>( 100 );
    ExternalGpsSource gpsSource( signalBufferPtr );
    gpsSource.init( 1, 1, 0, 32 );
    gpsSource.start();
    gpsSource.setLocation( 52.5761, 12.5761 );
    gpsSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );

    CollectedSignal firstSignal;
    CollectedSignal secondSignal;
    WAIT_ASSERT_TRUE( signalBufferPtr->pop( firstSignal ) );
    ASSERT_TRUE( signalBufferPtr->pop( secondSignal ) );
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, 12.5761, 0.0001 );
}

// Test longitude west
TEST_F( ExternalGpsSourceTest, testWestNegativeLongitude ) // NOLINT
{
    SignalBufferPtr signalBufferPtr = std::make_shared<SignalBuffer>( 100 );
    ExternalGpsSource gpsSource( signalBufferPtr );
    gpsSource.init( 1, 1, 0, 32 );
    gpsSource.start();
    gpsSource.setLocation( 52.5761, -12.5761 );
    gpsSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );

    CollectedSignal firstSignal;
    CollectedSignal secondSignal;
    WAIT_ASSERT_TRUE( signalBufferPtr->pop( firstSignal ) );
    ASSERT_TRUE( signalBufferPtr->pop( secondSignal ) );
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, -12.5761, 0.0001 ); // negative number
}
