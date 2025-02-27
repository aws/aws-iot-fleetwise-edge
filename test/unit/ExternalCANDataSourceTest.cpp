// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/ExternalCANDataSource.h"
#include "aws/iotfleetwise/CANDataConsumer.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/MessageTypes.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <linux/can.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

static void
sendTestMessage( ExternalCANDataSource &dataSource,
                 InterfaceID interfaceId,
                 uint32_t messageId = 0x123,
                 Timestamp timestamp = 0 )
{
    std::vector<uint8_t> data( 8 );
    for ( uint8_t i = 0; i < 8; ++i )
    {
        data[i] = i;
    }
    dataSource.ingestMessage( interfaceId, timestamp, messageId, data );
}

static void
sendTestFDMessage( ExternalCANDataSource &dataSource,
                   InterfaceID interfaceId,
                   uint32_t messageId = 0x123,
                   Timestamp timestamp = 0 )
{
    std::vector<uint8_t> data( 64 );
    for ( uint8_t i = 0; i < 64; ++i )
    {
        data[i] = i;
    }
    dataSource.ingestMessage( interfaceId, timestamp, messageId, data );
}

static void
sendTestMessageExtendedID( ExternalCANDataSource &dataSource,
                           InterfaceID interfaceId,
                           uint32_t messageId = 0x123,
                           Timestamp timestamp = 0 )
{
    std::vector<uint8_t> data( 8 );
    for ( uint8_t i = 0; i < 8; ++i )
    {
        data[i] = i;
    }
    dataSource.ingestMessage( interfaceId, timestamp, messageId | CAN_EFF_FLAG, data );
}

class ExternalCANDataSourceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> frameMap;
        CANMessageDecoderMethod decoderMethod;

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
        sigFormat1.mSignalType = SignalType::DOUBLE;

        CANSignalFormat sigFormat2;
        sigFormat2.mSignalID = 7;
        sigFormat2.mIsBigEndian = true;
        sigFormat2.mIsSigned = true;
        sigFormat2.mFirstBitPosition = 56;
        sigFormat2.mSizeInBits = 31;
        sigFormat2.mOffset = 0.0;
        sigFormat2.mFactor = 1.0;
        sigFormat2.mSignalType = SignalType::DOUBLE;

        decoderMethod.format.mSignals.push_back( sigFormat1 );
        decoderMethod.format.mSignals.push_back( sigFormat2 );
        frameMap[0x123] = decoderMethod;
        mDictionary = std::make_shared<CANDecoderDictionary>();
        mDictionary->canMessageDecoderMethod[0] = frameMap;
        mDictionary->signalIDsToCollect.emplace( 1 );
        mDictionary->signalIDsToCollect.emplace( 7 );

        mCANInterfaceIDTranslator.add( "mycan0" );
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<CANDecoderDictionary> mDictionary;
    CANInterfaceIDTranslator mCANInterfaceIDTranslator;
};

TEST_F( ExternalCANDataSourceTest, testNoDecoderDictionary )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 10, "Signal Buffer" );
    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );

    CANDataConsumer consumer{ signalBufferDistributor };
    ExternalCANDataSource dataSource{ mCANInterfaceIDTranslator, consumer };
    CollectedDataFrame collectedDataFrame;
    sendTestMessage( dataSource, "mycan0" );
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

TEST_F( ExternalCANDataSourceTest, testValidDecoderDictionary )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 10, "Signal Buffer" );

    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );

    CANDataConsumer consumer{ signalBufferDistributor };
    ExternalCANDataSource dataSource{ mCANInterfaceIDTranslator, consumer };
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedDataFrame collectedDataFrame;
    sendTestMessage( dataSource, "mycan0" );
    ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
    signal = collectedDataFrame.mCollectedSignals[1];
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );

    // Test message a different message ID and non-monotonic time is not received
    sendTestMessage( dataSource, "mycan0", 0x456, 1 );
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );

    // Test invalidation of decoder dictionary
    dataSource.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::RAW_SOCKET );
    sendTestMessage( dataSource, "mycan0" );
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
    // Check it ignores dictionaries for other protocols
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::OBD );
    sendTestMessage( dataSource, "mycan0" );
    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

TEST_F( ExternalCANDataSourceTest, testCanFDSocketMode )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 10, "Signal Buffer" );

    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );

    CANDataConsumer consumer{ signalBufferDistributor };
    ExternalCANDataSource dataSource{ mCANInterfaceIDTranslator, consumer };
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedDataFrame collectedDataFrame;
    sendTestFDMessage( dataSource, "mycan0" );
    ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );

    signal = collectedDataFrame.mCollectedSignals[1];
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );

    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

TEST_F( ExternalCANDataSourceTest, testExtractExtendedID )
{
    auto signalBuffer = std::make_shared<SignalBuffer>( 10, "Signal Buffer" );

    SignalBufferDistributor signalBufferDistributor;
    signalBufferDistributor.registerQueue( signalBuffer );

    CANDataConsumer consumer{ signalBufferDistributor };
    ExternalCANDataSource dataSource{ mCANInterfaceIDTranslator, consumer };
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedDataFrame collectedDataFrame;
    sendTestMessageExtendedID( dataSource, "mycan0" );
    ASSERT_TRUE( signalBuffer->pop( collectedDataFrame ) );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );

    signal = collectedDataFrame.mCollectedSignals[1];
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );

    ASSERT_FALSE( signalBuffer->pop( collectedDataFrame ) );
}

} // namespace IoTFleetWise
} // namespace Aws
