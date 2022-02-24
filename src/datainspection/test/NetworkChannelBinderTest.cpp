
/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "NetworkChannelBinder.h"
#include "IActiveDecoderDictionaryListener.h"
#include "NetworkChannelConsumer.h"
#include "businterfaces/SocketCANBusChannel.h"
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
#include <unistd.h>

using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::DataManagement;

static int
setup()
{
    // Setup a socket
    std::string socketCANIFName( "vcan0" );
    struct sockaddr_can interfaceAddress;
    struct ifreq interfaceRequest;

    if ( socketCANIFName.size() >= sizeof( interfaceRequest.ifr_name ) )
    {
        return -1;
    }

    int type = SOCK_RAW | SOCK_NONBLOCK;
    int socketFD = socket( PF_CAN, type, CAN_RAW );
    if ( socketFD < 0 )
    {
        return -1;
    }

    // Set the IF Name, address
    (void)strncpy( interfaceRequest.ifr_name, socketCANIFName.c_str(), sizeof( interfaceRequest.ifr_name ) - 1U );

    if ( ioctl( socketFD, SIOCGIFINDEX, &interfaceRequest ) )
    {
        close( socketFD );
        return -1;
    }

    memset( &interfaceAddress, 0, sizeof( interfaceAddress ) );
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = interfaceRequest.ifr_ifindex;

    if ( bind( socketFD, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        close( socketFD );
        return -1;
    }

    return socketFD;
}

static void
cleanUp( int socketFD )
{
    close( socketFD );
}

// 0x0408004019011008
static void
sendTestMessage( int socketFD, struct can_frame &frame )
{
    frame.can_id = 0x224;
    frame.can_dlc = 8;
    std::string payload = "0408004019011008";
    int j = 0;
    for ( unsigned int i = 0; i < payload.length(); i += 2 )
    {
        std::string byteString = payload.substr( i, 2 );
        uint8_t byte = (uint8_t)strtol( byteString.c_str(), NULL, 16 );
        frame.data[j] = byte;
        j++;
    }

    ASSERT_TRUE( write( socketFD, &frame, sizeof( struct can_frame ) ) > 0 );
}

static CANDecoderDictionary
generateDecoderDictionary1()
{
    // Below section construct decoder dictionary
    std::vector<CANSignalFormat> signals = std::vector<CANSignalFormat>( 1 );
    struct CANMessageFormat canMessageFormat;

    canMessageFormat.mMessageID = 0x224;
    canMessageFormat.mSizeInBytes = 8;
    canMessageFormat.mIsMultiplexed = false;
    canMessageFormat.mSignals = signals;

    struct CANMessageDecoderMethod decodeMethod = {
        CANMessageCollectType::RAW,
        canMessageFormat,
    };
    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecode{ { 0x224, decodeMethod } };
    CANDecoderDictionary dict = { { { 0, canIdToDecode }, { 1, canIdToDecode } }, { 0x123 } };
    return dict;
}

static CANDecoderDictionary
generateDecoderDictionary2()
{
    // Below section construct decoder dictionary
    struct CANMessageFormat canMessageFormat;

    std::vector<CANSignalFormat> signals = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 0x123;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    signals.emplace_back( sigFormat1 );

    canMessageFormat.mMessageID = 0x224;
    canMessageFormat.mSizeInBytes = 8;
    canMessageFormat.mIsMultiplexed = false;
    canMessageFormat.mSignals = signals;

    struct CANMessageDecoderMethod decodeMethod = {
        CANMessageCollectType::DECODE,
        canMessageFormat,
    };
    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecode{ { 0x224, decodeMethod } };
    CANDecoderDictionary dict = { { { 0, canIdToDecode }, { 1, canIdToDecode } }, { 0x123 } };
    return dict;
}

static CANDecoderDictionary
generateDecoderDictionary3()
{
    // Below section construct decoder dictionary
    struct CANMessageFormat canMessageFormat;

    std::vector<CANSignalFormat> signals = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 0x123;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    signals.emplace_back( sigFormat1 );

    canMessageFormat.mMessageID = 0x224;
    canMessageFormat.mSizeInBytes = 8;
    canMessageFormat.mIsMultiplexed = false;
    canMessageFormat.mSignals = signals;

    struct CANMessageDecoderMethod decodeMethod = {
        CANMessageCollectType::DECODE,
        canMessageFormat,
    };
    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecode{ { 0x224, decodeMethod } };
    CANDecoderDictionary dict = { { { 0, canIdToDecode }, { 1, canIdToDecode } }, { 0x111 } };
    return dict;
}

static CANDecoderDictionary
generateDecoderDictionary4()
{
    // Below section construct decoder dictionary
    struct CANMessageFormat canMessageFormat0;
    std::vector<CANSignalFormat> signals0 = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 0x123;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    signals0.emplace_back( sigFormat1 );

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 0x111;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 24;
    sigFormat2.mSizeInBits = 16;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;
    signals0.emplace_back( sigFormat2 );

    canMessageFormat0.mMessageID = 0x224;
    canMessageFormat0.mSizeInBytes = 8;
    canMessageFormat0.mIsMultiplexed = false;
    canMessageFormat0.mSignals = signals0;

    struct CANMessageDecoderMethod decodeMethod0 = {
        CANMessageCollectType::DECODE,
        canMessageFormat0,
    };

    struct CANMessageFormat canMessageFormat1;
    std::vector<CANSignalFormat> signals1 = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat3;
    sigFormat3.mSignalID = 0x528;
    sigFormat3.mIsBigEndian = false;
    sigFormat3.mIsSigned = false;
    sigFormat3.mFirstBitPosition = 0;
    sigFormat3.mSizeInBits = 16;
    sigFormat3.mOffset = 0.0;
    sigFormat3.mFactor = 1.0;
    signals1.emplace_back( sigFormat3 );

    CANSignalFormat sigFormat4;
    sigFormat4.mSignalID = 0x411;
    sigFormat4.mIsBigEndian = true;
    sigFormat4.mIsSigned = false;
    sigFormat4.mFirstBitPosition = 24;
    sigFormat4.mSizeInBits = 16;
    sigFormat4.mOffset = 0.0;
    sigFormat4.mFactor = 1.0;
    signals1.emplace_back( sigFormat4 );

    canMessageFormat1.mMessageID = 0x224;
    canMessageFormat1.mSizeInBytes = 8;
    canMessageFormat1.mIsMultiplexed = false;
    canMessageFormat1.mSignals = signals1;

    struct CANMessageDecoderMethod decodeMethod1 = {
        CANMessageCollectType::DECODE,
        canMessageFormat1,
    };

    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecodeBus0{ { 0x224, decodeMethod0 } };
    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecodeBus1{ { 0x224, decodeMethod1 } };
    CANDecoderDictionary dict = { { { 0, canIdToDecodeBus0 }, { 1, canIdToDecodeBus1 } }, { 0x111, 0x528 } };
    return dict;
}

static CANDecoderDictionary
generateDecoderDictionary5()
{
    // Below section construct decoder dictionary
    struct CANMessageFormat canMessageFormat0;
    std::vector<CANSignalFormat> signals0 = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 0x123;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    signals0.emplace_back( sigFormat1 );

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 0x111;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 24;
    sigFormat2.mSizeInBits = 16;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;
    signals0.emplace_back( sigFormat2 );

    canMessageFormat0.mMessageID = 0x224;
    canMessageFormat0.mSizeInBytes = 8;
    canMessageFormat0.mIsMultiplexed = false;
    canMessageFormat0.mSignals = signals0;

    struct CANMessageDecoderMethod decodeMethod0 = {
        CANMessageCollectType::DECODE,
        canMessageFormat0,
    };

    struct CANMessageFormat canMessageFormat1;
    std::vector<CANSignalFormat> signals1 = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat3;
    sigFormat3.mSignalID = 0x528;
    sigFormat3.mIsBigEndian = false;
    sigFormat3.mIsSigned = false;
    sigFormat3.mFirstBitPosition = 0;
    sigFormat3.mSizeInBits = 16;
    sigFormat3.mOffset = 0.0;
    sigFormat3.mFactor = 1.0;
    signals1.emplace_back( sigFormat3 );

    CANSignalFormat sigFormat4;
    sigFormat4.mSignalID = 0x411;
    sigFormat4.mIsBigEndian = true;
    sigFormat4.mIsSigned = false;
    sigFormat4.mFirstBitPosition = 24;
    sigFormat4.mSizeInBits = 16;
    sigFormat4.mOffset = 0.0;
    sigFormat4.mFactor = 1.0;
    signals1.emplace_back( sigFormat4 );

    canMessageFormat1.mMessageID = 0x224;
    canMessageFormat1.mSizeInBytes = 8;
    canMessageFormat1.mIsMultiplexed = false;
    canMessageFormat1.mSignals = signals1;

    struct CANMessageDecoderMethod decodeMethod1 = {
        CANMessageCollectType::DECODE,
        canMessageFormat1,
    };

    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecodeBus0{ { 0x224, decodeMethod0 } };
    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecodeBus1{ { 0x224, decodeMethod1 } };
    std::unordered_set<SignalID> signalIDsToCollect = { 0x111, 0x123, 0x528, 0x411 };
    std::unordered_map<CANChannelNumericID, std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>> method = {
        { 0, canIdToDecodeBus0 }, { 1, canIdToDecodeBus1 } };
    CANDecoderDictionary dict = { method, signalIDsToCollect };
    return dict;
}

static CANDecoderDictionary
generateDecoderDictionary6()
{
    // Below section construct decoder dictionary
    struct CANMessageFormat canMessageFormat0;
    std::vector<CANSignalFormat> signals0 = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 0x123;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    signals0.emplace_back( sigFormat1 );

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 0x111;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 24;
    sigFormat2.mSizeInBits = 16;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;
    signals0.emplace_back( sigFormat2 );

    canMessageFormat0.mMessageID = 0x224;
    canMessageFormat0.mSizeInBytes = 8;
    canMessageFormat0.mIsMultiplexed = false;
    canMessageFormat0.mSignals = signals0;

    struct CANMessageDecoderMethod decodeMethod0 = {
        CANMessageCollectType::RAW_AND_DECODE,
        canMessageFormat0,
    };

    struct CANMessageFormat canMessageFormat1;
    std::vector<CANSignalFormat> signals1 = std::vector<CANSignalFormat>();
    CANSignalFormat sigFormat3;
    sigFormat3.mSignalID = 0x528;
    sigFormat3.mIsBigEndian = false;
    sigFormat3.mIsSigned = false;
    sigFormat3.mFirstBitPosition = 0;
    sigFormat3.mSizeInBits = 16;
    sigFormat3.mOffset = 0.0;
    sigFormat3.mFactor = 1.0;
    signals1.emplace_back( sigFormat3 );

    CANSignalFormat sigFormat4;
    sigFormat4.mSignalID = 0x411;
    sigFormat4.mIsBigEndian = true;
    sigFormat4.mIsSigned = false;
    sigFormat4.mFirstBitPosition = 24;
    sigFormat4.mSizeInBits = 16;
    sigFormat4.mOffset = 0.0;
    sigFormat4.mFactor = 1.0;
    signals1.emplace_back( sigFormat4 );

    canMessageFormat1.mMessageID = 0x224;
    canMessageFormat1.mSizeInBytes = 8;
    canMessageFormat1.mIsMultiplexed = false;
    canMessageFormat1.mSignals = signals1;

    struct CANMessageDecoderMethod decodeMethod1 = {
        CANMessageCollectType::RAW_AND_DECODE,
        canMessageFormat1,
    };

    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecodeBus0{ { 0x224, decodeMethod0 } };
    std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> canIdToDecodeBus1{ { 0x224, decodeMethod1 } };
    std::unordered_set<SignalID> signalIDsToCollect = { 0x111, 0x123, 0x528, 0x411 };
    std::unordered_map<CANChannelNumericID, std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>> method = {
        { 0, canIdToDecodeBus0 }, { 1, canIdToDecodeBus1 } };
    CANDecoderDictionary dict = { method, signalIDsToCollect };
    return dict;
}

class Finally
{
public:
    Finally( std::function<void()> func )
        : mFunc( func ){};
    ~Finally()
    {
        mFunc();
    }

private:
    std::function<void()> mFunc;
};

class NetworkChannelBinderTest : public ::testing::Test
{
public:
    int socketFD;
    std::shared_ptr<INetworkChannelBridge> channelPtr;
    std::shared_ptr<INetworkChannelBridge> channelPtr2;
    std::shared_ptr<INetworkChannelConsumer> consumerPtr;
    std::shared_ptr<INetworkChannelConsumer> consumerPtr2;
    std::shared_ptr<const CANDecoderDictionary> dictionarySharedPtr;
    NetworkChannelID channelID;
    NetworkChannelID channelID2;
    NetworkChannelBinder binder;

protected:
    void
    SetUp() override
    {
        socketFD = setup();

        channelPtr.reset( new SocketCANBusChannel( "vcan0", true ) );
        channelPtr2.reset( new SocketCANBusChannel( "vcan0", true ) );
        ASSERT_TRUE( channelPtr->init( 10000, 1000 ) );
        ASSERT_EQ( channelPtr->getChannelType(), NetworkChannelType::CAN_CHANNEL );
        ASSERT_TRUE( channelPtr2->init( 10000, 1000 ) );
        channelID = channelPtr->getChannelID();
        channelID2 = channelPtr2->getChannelID();

        // three buffers
        auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
        auto canRawBufferPtr = std::make_shared<CANBuffer>( 256 );

        consumerPtr.reset( new NetworkChannelConsumer() );
        consumerPtr2.reset( new NetworkChannelConsumer() );

        ASSERT_TRUE( consumerPtr->init( 0, signalBufferPtr, canRawBufferPtr, 1000 ) );
        ASSERT_TRUE( consumerPtr2->init( 1, signalBufferPtr, canRawBufferPtr, 1000 ) );

        ASSERT_TRUE( binder.connect() );
        ASSERT_TRUE( binder.addNetworkChannel( channelPtr ) );
        ASSERT_TRUE( binder.addNetworkChannel( channelPtr2 ) );
        ASSERT_TRUE( binder.bindConsumerToNetworkChannel( consumerPtr, channelID ) );
        ASSERT_TRUE( binder.bindConsumerToNetworkChannel( consumerPtr2, channelID2 ) );

        dictionarySharedPtr = std::make_shared<const CANDecoderDictionary>();
        binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );
    }

    void
    TearDown() override
    {
        ASSERT_TRUE( binder.disconnect() );
        cleanUp( socketFD );
    }
};

/** @brief  In this test, raw CAN Frame to be collected based on decoder dictionary
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestCollectCANFrame_LinuxCANDep )
{
    dictionarySharedPtr = std::make_shared<const CANDecoderDictionary>( generateDecoderDictionary1() );

    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );

    struct can_frame frame = {};
    sendTestMessage( socketFD, frame );
    // Sleep for sometime on this thread to allow the other thread to finish
    // As SocketCanBusChannel and NetworkChannelConsumer wait 1 second we must
    // wait more than 2 seconds here
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    // Verify raw CAN Frames are collected correctly
    CollectedCanRawFrame rawCANMsg;
    ASSERT_TRUE( consumerPtr->getCANBufferPtr()->pop( rawCANMsg ) );
    ASSERT_EQ( 8, rawCANMsg.size );
    for ( int i = 0; i < rawCANMsg.size; ++i )
    {
        ASSERT_EQ( frame.data[i], rawCANMsg.data[i] );
    }
    ASSERT_TRUE( consumerPtr2->getCANBufferPtr()->pop( rawCANMsg ) );
    ASSERT_EQ( 8, rawCANMsg.size );
    for ( int i = 0; i < rawCANMsg.size; ++i )
    {
        ASSERT_EQ( frame.data[i], rawCANMsg.data[i] );
    }
}

/** @brief In this test, first signal in the CAN Frame will be collected based on decoder dictionary
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestCollectSelectedSignalBuffer_LinuxCANDep )
{

    dictionarySharedPtr = std::make_shared<const CANDecoderDictionary>( generateDecoderDictionary2() );

    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );

    struct can_frame frame = {};
    sendTestMessage( socketFD, frame );
    // Sleep for sometime on this thread to allow the other thread to finish
    // As SocketCanBusChannel and NetworkChannelConsumer wait 1 second we must
    // wait more than 2 seconds here
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    // Verify signals are decoded and collected correctly
    CollectedSignal signal;
    ASSERT_TRUE( consumerPtr->getSignalBufferPtr()->pop( signal ) );
    ASSERT_EQ( 0x123, signal.signalID );
    ASSERT_DOUBLE_EQ( 0x0804, signal.value );
    ASSERT_TRUE( consumerPtr2->getSignalBufferPtr()->pop( signal ) );
    ASSERT_EQ( 0x123, signal.signalID );
    ASSERT_DOUBLE_EQ( 0x0804, signal.value );
}

/** @brief In this test, no signals in the CAN Frame will be collected based on decoder dictionary
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestNotCollectSignalBuffer_LinuxCANDep )
{
    dictionarySharedPtr = std::make_shared<const CANDecoderDictionary>( generateDecoderDictionary3() );

    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );

    struct can_frame frame = {};
    sendTestMessage( socketFD, frame );
    // Sleep for sometime on this thread to allow the other thread to finish
    // As SocketCanBusChannel and NetworkChannelConsumer wait 1 second we must
    // wait more than 2 seconds here
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    // Verify NO signals are decoded and collected based on decoder dictionary
    CollectedSignal signal;
    ASSERT_FALSE( consumerPtr->getSignalBufferPtr()->pop( signal ) );
    ASSERT_FALSE( consumerPtr2->getSignalBufferPtr()->pop( signal ) );
}

/** @brief In this test, only one signal from the CAN Frame to
 * be collected based on decoder dictionary
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestCollectSignalBuffer2_LinuxCANDep )
{
    dictionarySharedPtr = std::make_shared<CANDecoderDictionary>( generateDecoderDictionary4() );

    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );

    struct can_frame frame = {};

    auto f = []( int socketFD, struct can_frame frame, int msgCnt ) {
        for ( int i = 0; i < msgCnt; ++i )
        {
            sendTestMessage( socketFD, frame );
        }
    };

    std::thread canMessageThread( f, socketFD, frame, 1 );
    canMessageThread.join();
    // Sleep for sometime on this thread to allow the other thread to finish
    // As SocketCanBusChannel and NetworkChannelConsumer wait 1 second we must
    // wait more than 2 seconds here
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    // Verify signals are decoded and collected correctly
    std::unordered_map<SignalID, double> collectedSignals;
    CollectedSignal signal;
    while ( !consumerPtr->getSignalBufferPtr()->empty() )
    {
        consumerPtr->getSignalBufferPtr()->pop( signal );
        collectedSignals[signal.signalID] = signal.value;
    }
    ASSERT_EQ( 2, collectedSignals.size() );
    ASSERT_EQ( 1, collectedSignals.count( 0x111 ) );
    ASSERT_EQ( 1, collectedSignals.count( 0x528 ) );
    if ( collectedSignals.count( 0x111 ) > 0 )
    {
        ASSERT_EQ( 0x0040, collectedSignals[0x111] );
    }
    if ( collectedSignals.count( 0x528 ) > 0 )
    {
        ASSERT_EQ( 0x0804, collectedSignals[0x528] );
    }
}

/** @brief In this test, two signals from the CAN Frame on Both two Channels to be collected
 * based on decoder dictionary
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestCollectSignalBuffer3_LinuxCANDep )
{
    dictionarySharedPtr = std::make_shared<const CANDecoderDictionary>( generateDecoderDictionary5() );

    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );

    struct can_frame frame = {};
    sendTestMessage( socketFD, frame );
    // Sleep for sometime on this thread to allow the other thread to finish
    // As SocketCanBusChannel and NetworkChannelConsumer wait 1 second we must
    // wait more than 2 seconds here
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    // Verify signals are decoded and collected correctly
    std::unordered_map<SignalID, double> collectedSignals;
    CollectedSignal signal;
    while ( !consumerPtr->getSignalBufferPtr()->empty() )
    {
        consumerPtr->getSignalBufferPtr()->pop( signal );
        collectedSignals[signal.signalID] = signal.value;
    }
    ASSERT_EQ( 4, collectedSignals.size() );
    ASSERT_EQ( 1, collectedSignals.count( 0x111 ) );
    ASSERT_EQ( 0x0040, collectedSignals[0x111] );
    ASSERT_EQ( 1, collectedSignals.count( 0x528 ) );
    ASSERT_EQ( 0x0804, collectedSignals[0x528] );
    ASSERT_EQ( 1, collectedSignals.count( 0x411 ) );
    ASSERT_EQ( 0x0040, collectedSignals[0x411] );
    ASSERT_EQ( 1, collectedSignals.count( 0x123 ) );
    ASSERT_EQ( 0x0804, collectedSignals[0x123] );
}

/** @brief In this test, two RAW CAN Frames and two signals from the CAN Frame on Both two Channels
 * to be collected based on decoder dictionary
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestCollectSignalBufferAndRawFrame_LinuxCANDep )
{
    dictionarySharedPtr = std::make_shared<const CANDecoderDictionary>( generateDecoderDictionary6() );

    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );

    struct can_frame frame = {};
    sendTestMessage( socketFD, frame );
    // Sleep for sometime on this thread to allow the other thread to finish
    // As SocketCanBusChannel and NetworkChannelConsumer wait 1 second we must
    // wait more than 2 seconds here
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    // Verify signals are decoded and collected correctly
    std::unordered_map<SignalID, double> collectedSignals;
    CollectedSignal signal;
    while ( !consumerPtr->getSignalBufferPtr()->empty() )
    {
        consumerPtr->getSignalBufferPtr()->pop( signal );
        collectedSignals[signal.signalID] = signal.value;
    }
    ASSERT_EQ( 4, collectedSignals.size() );
    ASSERT_EQ( 1, collectedSignals.count( 0x111 ) );
    ASSERT_EQ( 0x0040, collectedSignals[0x111] );
    ASSERT_EQ( 1, collectedSignals.count( 0x528 ) );
    ASSERT_EQ( 0x0804, collectedSignals[0x528] );
    ASSERT_EQ( 1, collectedSignals.count( 0x411 ) );
    ASSERT_EQ( 0x0040, collectedSignals[0x411] );
    ASSERT_EQ( 1, collectedSignals.count( 0x123 ) );
    ASSERT_EQ( 0x0804, collectedSignals[0x123] );

    // Verify CAN RAW Frame is collected as well
    CollectedCanRawFrame rawCANMsg;
    ASSERT_TRUE( consumerPtr->getCANBufferPtr()->pop( rawCANMsg ) );
    ASSERT_EQ( 8, rawCANMsg.size );
    for ( int i = 0; i < rawCANMsg.size; ++i )
    {
        ASSERT_EQ( frame.data[i], rawCANMsg.data[i] );
    }
    ASSERT_TRUE( consumerPtr2->getCANBufferPtr()->pop( rawCANMsg ) );
    ASSERT_EQ( 8, rawCANMsg.size );
    for ( int i = 0; i < rawCANMsg.size; ++i )
    {
        ASSERT_EQ( frame.data[i], rawCANMsg.data[i] );
    }
}

/** @brief In this test, decoder dictionary update invoked in the middle of message decoding
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestChangeDecoderDictionaryDuringDecoding_LinuxCANDep )
{
    dictionarySharedPtr = std::make_shared<CANDecoderDictionary>( generateDecoderDictionary4() );
    struct can_frame frame = {};

    auto f = []( int socketFD, struct can_frame frame, int msgCnt ) {
        for ( int i = 0; i < msgCnt; ++i )
        {
            sendTestMessage( socketFD, frame );
        }
    };
    // At this point, the decoder dictionary is still empty with no decoding rule
    // Generate a long duration of CAN decoding scenario by sending 256 messages
    std::thread canMessageThread( f, socketFD, frame, 256 );

    // Sleep for one second for canMessageThread to send out some messages
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    // update dictionary while thread is still busy decoding. This test check whether Network Channel
    // Consumer can gracefully handle such multi thread scenario.
    binder.onChangeOfActiveDictionary( dictionarySharedPtr, RAW_SOCKET );
    canMessageThread.join();
    std::this_thread::sleep_for( std::chrono::seconds( 4 ) );
    // Verify signals are decoded and collected correctly
    std::unordered_map<SignalID, double> collectedSignals;
    CollectedSignal signal;
    while ( !consumerPtr->getSignalBufferPtr()->empty() )
    {
        consumerPtr->getSignalBufferPtr()->pop( signal );
        collectedSignals[signal.signalID] = signal.value;
    }
    // make sure we still receive the two signals.
    ASSERT_EQ( 2, collectedSignals.size() );
    ASSERT_EQ( 1, collectedSignals.count( 0x111 ) );
    ASSERT_EQ( 1, collectedSignals.count( 0x528 ) );
    if ( collectedSignals.count( 0x111 ) > 0 )
    {
        ASSERT_EQ( 0x0040, collectedSignals[0x111] );
    }
    if ( collectedSignals.count( 0x528 ) > 0 )
    {
        ASSERT_EQ( 0x0804, collectedSignals[0x528] );
    }
}

/** @brief In this test, consumer received the data but cannot perform decoding due to missing
 *  decoder dictionary.
 *  We expect consumer to discard all the input data if decoder dictionary is not valid
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderTestDiscardInputBufferWhenDecoderDictionaryIsInValid_LinuxCANDep )
{
    // set decoder dictionary as invalid
    binder.onChangeOfActiveDictionary( nullptr, RAW_SOCKET );
    struct can_frame frame = {};
    auto f = []( int socketFD, struct can_frame frame, int msgCnt ) {
        for ( int i = 0; i < msgCnt; ++i )
        {
            sendTestMessage( socketFD, frame );
        }
    };
    // Create a separate thread to send 8 CAN Frames
    std::thread canMessageThread( f, socketFD, frame, 8 );
    // Wait until thread complete.
    canMessageThread.join();
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    // Verify no signals were collected due to missing decoder dictionary
    ASSERT_TRUE( consumerPtr->getSignalBufferPtr()->empty() );
    // Now we provide Network Channel Binder a valid decoder dictionary
    binder.onChangeOfActiveDictionary( std::make_shared<CANDecoderDictionary>( generateDecoderDictionary4() ),
                                       RAW_SOCKET );
    // As the network channels just woken up and the consumer input queue is empty, we should still
    // expect empty output queue
    ASSERT_TRUE( consumerPtr->getSignalBufferPtr()->empty() );
}

/** @brief In this test, we validate the startup and the shutdown APIs.
 */
TEST_F( NetworkChannelBinderTest, NetworkChannelBinderStartupAndShutdownCycle_LinuxCANDep )
{
    int socket;
    std::shared_ptr<INetworkChannelBridge> channel;
    std::shared_ptr<INetworkChannelConsumer> consumer;
    std::shared_ptr<const CANDecoderDictionary> dictionary;
    NetworkChannelID channelID;
    NetworkChannelBinder networkBinder;
    socket = setup();
    ASSERT_TRUE( socket != -1 );
    Finally finally( [=] { cleanUp( socket ); } );
    channel.reset( new SocketCANBusChannel( "vcan0", true ) );
    ASSERT_TRUE( channel->init( 10000, 1000 ) );
    channelID = channel->getChannelID();

    // Buffers
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto canRawBufferPtr = std::make_shared<CANBuffer>( 256 );

    consumer.reset( new NetworkChannelConsumer() );

    ASSERT_TRUE( consumer->init( 0, signalBufferPtr, canRawBufferPtr, 1000 ) );

    ASSERT_TRUE( networkBinder.connect() );
    ASSERT_TRUE( networkBinder.addNetworkChannel( channel ) );
    ASSERT_TRUE( networkBinder.bindConsumerToNetworkChannel( consumer, channelID ) );
    // Initially Channels and Consumers should be running but in a sleep mode
    // We send few messages on the bus and check that they are neither consumed
    // by the Channel nor by the consumer
    struct can_frame frame = {};
    sendTestMessage( socket, frame );
    // Sleep to accommodate the polling frequency of the threads.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( channel->getBuffer()->empty() );
    ASSERT_TRUE( consumer->getCANBufferPtr()->empty() );
    // Pass a null decoder manifest, and check that both the channel and the consumer
    // are still sleeping.
    binder.onChangeOfActiveDictionary( nullptr, RAW_SOCKET );
    sendTestMessage( socket, frame );
    // Sleep to accommodate the polling frequency of the threads.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( channel->getBuffer()->empty() );
    ASSERT_TRUE( consumer->getCANBufferPtr()->empty() );
    // Pass a correct decoder Manifest and Make sure that channel and consumer
    // consumed the data.
    dictionary = std::make_shared<const CANDecoderDictionary>( generateDecoderDictionary1() );
    networkBinder.onChangeOfActiveDictionary( dictionary, RAW_SOCKET );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    sendTestMessage( socket, frame );
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    ASSERT_FALSE( consumer->getCANBufferPtr()->empty() );
    ASSERT_TRUE( networkBinder.disconnect() );
}
