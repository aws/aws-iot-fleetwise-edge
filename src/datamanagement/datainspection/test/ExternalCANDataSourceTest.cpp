// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExternalCANDataSource.h"
#include <gtest/gtest.h>
#include <linux/can.h>

using namespace Aws::IoTFleetWise::DataInspection;

static void
sendTestMessage( ExternalCANDataSource &dataSource,
                 CANChannelNumericID channelId,
                 uint32_t messageId = 0x123,
                 Timestamp timestamp = 0 )
{
    std::vector<uint8_t> data( 8 );
    for ( uint8_t i = 0; i < 8; ++i )
    {
        data[i] = i;
    }
    dataSource.ingestMessage( channelId, timestamp, messageId, data );
}

static void
sendTestFDMessage( ExternalCANDataSource &dataSource,
                   CANChannelNumericID channelId,
                   uint32_t messageId = 0x123,
                   Timestamp timestamp = 0 )
{
    std::vector<uint8_t> data( 64 );
    for ( uint8_t i = 0; i < 64; ++i )
    {
        data[i] = i;
    }
    dataSource.ingestMessage( channelId, timestamp, messageId, data );
}

static void
sendTestMessageExtendedID( ExternalCANDataSource &dataSource,
                           CANChannelNumericID channelId,
                           uint32_t messageId = 0x123,
                           Timestamp timestamp = 0 )
{
    std::vector<uint8_t> data( 8 );
    for ( uint8_t i = 0; i < 8; ++i )
    {
        data[i] = i;
    }
    dataSource.ingestMessage( channelId, timestamp, messageId | CAN_EFF_FLAG, data );
}

class ExternalCANDataSourceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> frameMap;
        CANMessageDecoderMethod decoderMethod;
        decoderMethod.collectType = CANMessageCollectType::RAW_AND_DECODE;

        decoderMethod.format.mMessageID = 0x123;
        decoderMethod.format.mSizeInBytes = 8;

        CANSignalFormat sigFormat1;
        sigFormat1.mSignalID = 1;
        sigFormat1.mIsBigEndian = true;
        sigFormat1.mIsSigned = true;
        sigFormat1.mFirstBitPosition = 24;
        sigFormat1.mSizeInBits = 30;
        sigFormat1.mOffset = 0.0;
        sigFormat1.mFactor = 1.0;

        CANSignalFormat sigFormat2;
        sigFormat2.mSignalID = 7;
        sigFormat2.mIsBigEndian = true;
        sigFormat2.mIsSigned = true;
        sigFormat2.mFirstBitPosition = 56;
        sigFormat2.mSizeInBits = 31;
        sigFormat2.mOffset = 0.0;
        sigFormat2.mFactor = 1.0;

        decoderMethod.format.mSignals.push_back( sigFormat1 );
        decoderMethod.format.mSignals.push_back( sigFormat2 );
        frameMap[0x123] = decoderMethod;
        mDictionary = std::make_shared<CANDecoderDictionary>();
        mDictionary->canMessageDecoderMethod[0] = frameMap;
        mDictionary->signalIDsToCollect.emplace( 1 );
        mDictionary->signalIDsToCollect.emplace( 7 );
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<CANDecoderDictionary> mDictionary;
};

TEST_F( ExternalCANDataSourceTest, testNoDecoderDictionary )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    ExternalCANDataSource dataSource{ consumer };
    CollectedSignal signal;
    sendTestMessage( dataSource, 0 );
    ASSERT_FALSE( signalBufferPtr->pop( signal ) );
    CollectedCanRawFrame frame;
    ASSERT_FALSE( canRawBufferPtr->pop( frame ) );
}

TEST_F( ExternalCANDataSourceTest, testValidDecoderDictionary )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    ExternalCANDataSource dataSource{ consumer };
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedSignal signal;
    sendTestMessage( dataSource, 0 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );
    CollectedCanRawFrame frame;
    ASSERT_TRUE( canRawBufferPtr->pop( frame ) );
    ASSERT_EQ( frame.channelId, 0 );
    ASSERT_EQ( frame.frameID, 0x123 );
    ASSERT_EQ( frame.size, 8 );
    for ( auto i = 0; i < 8; i++ )
    {
        ASSERT_EQ( frame.data[i], i );
    }

    // Test message a different message ID and non-monotonic time is not received
    sendTestMessage( dataSource, 0, 0x456, 1 );
    ASSERT_FALSE( signalBufferPtr->pop( signal ) || canRawBufferPtr->pop( frame ) );

    // Test invalidation of decoder dictionary
    dataSource.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::RAW_SOCKET );
    sendTestMessage( dataSource, 0 );
    ASSERT_FALSE( signalBufferPtr->pop( signal ) || canRawBufferPtr->pop( frame ) );
    // Check it ignores dictionaries for other protocols
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::OBD );
    sendTestMessage( dataSource, 0 );
    ASSERT_FALSE( signalBufferPtr->pop( signal ) || canRawBufferPtr->pop( frame ) );
}

TEST_F( ExternalCANDataSourceTest, testCanFDSocketMode )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    ExternalCANDataSource dataSource{ consumer };
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedSignal signal;
    sendTestFDMessage( dataSource, 0 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );
    ASSERT_FALSE( signalBufferPtr->pop( signal ) );
    CollectedCanRawFrame frame;
    ASSERT_TRUE( canRawBufferPtr->pop( frame ) );
    ASSERT_EQ( frame.channelId, 0 );
    ASSERT_EQ( frame.frameID, 0x123 );
    ASSERT_EQ( frame.size, 64 );
    for ( auto i = 0; i < 64; i++ )
    {
        ASSERT_EQ( frame.data[i], i );
    }
    ASSERT_FALSE( signalBufferPtr->pop( signal ) );
}

TEST_F( ExternalCANDataSourceTest, testExtractExtendedID )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    ExternalCANDataSource dataSource{ consumer };
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedSignal signal;
    sendTestMessageExtendedID( dataSource, 0 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );
    ASSERT_FALSE( signalBufferPtr->pop( signal ) );
    CollectedCanRawFrame frame;
    ASSERT_TRUE( canRawBufferPtr->pop( frame ) );
    ASSERT_EQ( frame.channelId, 0 );
    ASSERT_EQ( frame.frameID, 0x123 );
    ASSERT_EQ( frame.size, 8 );
    for ( auto i = 0; i < 8; i++ )
    {
        ASSERT_EQ( frame.data[i], i );
    }
    ASSERT_FALSE( signalBufferPtr->pop( signal ) );
}
