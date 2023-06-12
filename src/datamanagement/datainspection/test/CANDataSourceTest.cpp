
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CANDataSource.h"
#include "WaitUntil.h"
#include <functional>
#include <gtest/gtest.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unordered_set>

using namespace Aws::IoTFleetWise::TestingSupport;
using namespace Aws::IoTFleetWise::DataInspection;

static void
cleanUp( int mSocketFD )
{
    close( mSocketFD );
}

static int
setup( bool fd = false )
{
    // Setup a socket
    std::string socketCANIFName( "vcan0" );
    struct sockaddr_can interfaceAddress;
    struct ifreq interfaceRequest;

    int type = SOCK_RAW | SOCK_NONBLOCK;
    int mSocketFD = socket( PF_CAN, type, CAN_RAW );
    if ( mSocketFD < 0 )
    {
        return -1;
    }
    if ( fd )
    {
        int canfd_on = 1;
        if ( setsockopt( mSocketFD, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof( canfd_on ) ) != 0 )
        {
            return -1;
        }
    }

    if ( socketCANIFName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        cleanUp( mSocketFD );
        return -1;
    }
    (void)strncpy( interfaceRequest.ifr_name, socketCANIFName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );

    if ( ioctl( mSocketFD, SIOCGIFINDEX, &interfaceRequest ) )
    {
        cleanUp( mSocketFD );
        return -1;
    }

    memset( &interfaceAddress, 0, sizeof( interfaceAddress ) );
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    if ( bind( mSocketFD, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        cleanUp( mSocketFD );
        return -1;
    }

    return mSocketFD;
}

static bool
sendTestMessage( int mSocketFD, uint32_t messageId = 0x123 )
{
    struct can_frame frame = {};
    frame.can_id = messageId;
    frame.can_dlc = 8;
    for ( uint8_t i = 0; i < 8; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( mSocketFD, &frame, sizeof( struct can_frame ) );
    EXPECT_EQ( bytesWritten, sizeof( struct can_frame ) );
    return true;
}

static bool
sendTestFDMessage( int mSocketFD )
{
    struct canfd_frame frame = {};
    frame.can_id = 0x123;
    frame.len = 64;
    for ( uint8_t i = 0; i < 64; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( mSocketFD, &frame, sizeof( struct canfd_frame ) );
    EXPECT_EQ( bytesWritten, sizeof( struct canfd_frame ) );
    return true;
}

static bool
sendTestMessageExtendedID( int mSocketFD )
{
    struct can_frame frame = {};
    frame.can_id = 0x123 | CAN_EFF_FLAG;

    frame.can_dlc = 8;
    for ( uint8_t i = 0; i < 8; ++i )
    {
        frame.data[i] = i;
    }
    ssize_t bytesWritten = write( mSocketFD, &frame, sizeof( struct can_frame ) );
    EXPECT_EQ( bytesWritten, sizeof( struct can_frame ) );
    return true;
}

class CANDataSourceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        mSocketFD = setup();
        if ( mSocketFD == -1 )
        {
            GTEST_SKIP() << "Skipping test fixture due to unavailability of socket";
        }
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
        cleanUp( mSocketFD );
    }

    int mSocketFD;
    std::shared_ptr<CANDecoderDictionary> mDictionary;
};

TEST_F( CANDataSourceTest, invalidInit )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    CANDataSource dataSource{
        INVALID_CAN_SOURCE_NUMERIC_ID, CanTimestampType::KERNEL_HARDWARE_TIMESTAMP, "vcan0", false, 100, consumer };
    ASSERT_FALSE( dataSource.init() );
}

TEST_F( CANDataSourceTest, testNoDecoderDictionary )
{
    ASSERT_TRUE( mSocketFD != -1 );
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    CANDataSource dataSource{ 0, CanTimestampType::KERNEL_HARDWARE_TIMESTAMP, "vcan0", false, 100, consumer };
    ASSERT_TRUE( dataSource.init() );
    ASSERT_TRUE( dataSource.isAlive() );
    CollectedSignal signal;
    DELAY_ASSERT_FALSE( sendTestMessage( mSocketFD ) && signalBufferPtr->pop( signal ) );
    CollectedCanRawFrame frame;
    ASSERT_FALSE( canRawBufferPtr->pop( frame ) );
    // No disconnect to test destructor disconnect
}

TEST_F( CANDataSourceTest, testValidDecoderDictionary )
{
    ASSERT_TRUE( mSocketFD != -1 );
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    CANDataSource dataSource{ 0, CanTimestampType::KERNEL_HARDWARE_TIMESTAMP, "vcan0", false, 100, consumer };
    ASSERT_TRUE( dataSource.init() );
    ASSERT_TRUE( dataSource.isAlive() );
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( sendTestMessage( mSocketFD ) && signalBufferPtr->pop( signal ) );
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

    // Test message a different message ID is not received
    DELAY_ASSERT_FALSE( sendTestMessage( mSocketFD, 0x456 ) &&
                        ( signalBufferPtr->pop( signal ) || canRawBufferPtr->pop( frame ) ) );

    // Test invalidation of decoder dictionary
    dataSource.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::RAW_SOCKET );
    DELAY_ASSERT_FALSE( sendTestMessage( mSocketFD ) &&
                        ( signalBufferPtr->pop( signal ) || canRawBufferPtr->pop( frame ) ) );
    // Check it ignores dictionaries for other protocols
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::OBD );
    DELAY_ASSERT_FALSE( sendTestMessage( mSocketFD ) &&
                        ( signalBufferPtr->pop( signal ) || canRawBufferPtr->pop( frame ) ) );
    ASSERT_TRUE( dataSource.disconnect() );
}

TEST_F( CANDataSourceTest, testCanFDSocketMode )
{
    cleanUp( mSocketFD );
    mSocketFD = setup( true );
    ASSERT_TRUE( mSocketFD != -1 );
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    CANDataSource dataSource{ 0, CanTimestampType::KERNEL_SOFTWARE_TIMESTAMP, "vcan0", false, 100, consumer };
    ASSERT_TRUE( dataSource.init() );
    ASSERT_TRUE( dataSource.isAlive() );
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( sendTestFDMessage( mSocketFD ) && signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );
    DELAY_ASSERT_FALSE( signalBufferPtr->pop( signal ) );
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
    ASSERT_TRUE( dataSource.disconnect() );
}

TEST_F( CANDataSourceTest, testExtractExtendedID )
{
    ASSERT_TRUE( mSocketFD != -1 );
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 10 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 10 );

    CANDataConsumer consumer{ signalBufferPtr, canRawBufferPtr };
    CANDataSource dataSource{ 0, CanTimestampType::KERNEL_HARDWARE_TIMESTAMP, "vcan0", false, 100, consumer };
    ASSERT_TRUE( dataSource.init() );
    ASSERT_TRUE( dataSource.isAlive() );
    dataSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( sendTestMessageExtendedID( mSocketFD ) && signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_EQ( signal.signalID, 1 );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
    ASSERT_TRUE( signalBufferPtr->pop( signal ) );
    ASSERT_EQ( signal.signalID, 7 );
    ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );
    DELAY_ASSERT_FALSE( signalBufferPtr->pop( signal ) );
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
    ASSERT_TRUE( dataSource.disconnect() );
}

TEST_F( CANDataSourceTest, testStringToCanTimestampType )
{
    CanTimestampType timestampType;
    ASSERT_TRUE( stringToCanTimestampType( "Software", timestampType ) );
    ASSERT_EQ( timestampType, CanTimestampType::KERNEL_SOFTWARE_TIMESTAMP );
    ASSERT_TRUE( stringToCanTimestampType( "Hardware", timestampType ) );
    ASSERT_EQ( timestampType, CanTimestampType::KERNEL_HARDWARE_TIMESTAMP );
    ASSERT_TRUE( stringToCanTimestampType( "Polling", timestampType ) );
    ASSERT_EQ( timestampType, CanTimestampType::POLLING_TIME );
    ASSERT_FALSE( stringToCanTimestampType( "abc", timestampType ) );
}
