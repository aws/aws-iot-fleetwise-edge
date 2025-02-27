// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/SomeipToCanBridge.h"
#include "SomeipMock.h"
#include "aws/iotfleetwise/CANDataConsumer.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/MessageTypes.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <vsomeip/vsomeip.hpp>

#define SOMEIP_SERVICE_ID 0x1234
#define SOMEIP_INSTANCE_ID 0x5678
#define SOMEIP_EVENT_ID 0x9ABC
#define SOMEIP_EVENTGROUP_ID 0xDEF0
#define SOMEIP_APPLICATION_NAME "someip_app"
#define CAN_ID 0x123
#define CAN_RECEIVE_TIME 0x0123456789ABCDEF

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::An;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

class SomeipToCanBridgeTest : public ::testing::Test
{
protected:
    SomeipToCanBridgeTest()
        : mSignalBuffer( std::make_shared<SignalBuffer>( 3, "Signal Buffer" ) )
        , mCanConsumer( mSignalBufferDistributor )
        , mSomeipApplicationMock( std::make_shared<StrictMock<SomeipApplicationMock>>() )
        , mSomeipToCanBridge(
              SOMEIP_SERVICE_ID,
              SOMEIP_INSTANCE_ID,
              SOMEIP_EVENT_ID,
              SOMEIP_EVENTGROUP_ID,
              SOMEIP_APPLICATION_NAME,
              0,
              mCanConsumer,
              [this]( std::string ) {
                  return mSomeipApplicationMock;
              },
              []( std::string ) {} )
        , mMessage( vsomeip::runtime::get()->create_message() )
    {
    }

    void
    SetUp() override
    {
        std::unordered_map<CANRawFrameID, CANMessageDecoderMethod> frameMap;
        CANMessageDecoderMethod decoderMethod;

        decoderMethod.format.mMessageID = CAN_ID;
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
        frameMap[CAN_ID] = decoderMethod;
        mDictionary = std::make_shared<CANDecoderDictionary>();
        mDictionary->canMessageDecoderMethod[0] = frameMap;
        mDictionary->signalIDsToCollect.emplace( 1 );
        mDictionary->signalIDsToCollect.emplace( 7 );

        setupCanMessage( CAN_ID, CAN_RECEIVE_TIME, { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 } );

        mSignalBufferDistributor.registerQueue( mSignalBuffer );
    }

    void
    TearDown() override
    {
        mSomeipToCanBridge.disconnect();
    }

    void
    setupInitExpectations()
    {
        EXPECT_CALL( *mSomeipApplicationMock, init() ).Times( 1 ).WillOnce( Return( true ) );
        EXPECT_CALL( *mSomeipApplicationMock, register_sd_acceptance_handler( _ ) )
            .Times( 1 )
            .WillOnce( SaveArg<0>( &mServiceDiscoveryAcceptanceHandler ) );
        EXPECT_CALL( *mSomeipApplicationMock,
                     register_availability_handler(
                         SOMEIP_SERVICE_ID, SOMEIP_INSTANCE_ID, An<const vsomeip::availability_handler_t &>(), _, _ ) )
            .Times( 1 )
            .WillOnce( SaveArg<2>( &mAvailabilityHandler ) );
        EXPECT_CALL( *mSomeipApplicationMock, request_service( SOMEIP_SERVICE_ID, SOMEIP_INSTANCE_ID, _, _ ) )
            .Times( 1 );
        EXPECT_CALL( *mSomeipApplicationMock,
                     register_message_handler( SOMEIP_SERVICE_ID, SOMEIP_INSTANCE_ID, SOMEIP_EVENT_ID, _ ) )
            .Times( 1 )
            .WillOnce( SaveArg<3>( &mMessageHandler ) );
        EXPECT_CALL(
            *mSomeipApplicationMock,
            request_event(
                SOMEIP_SERVICE_ID, SOMEIP_INSTANCE_ID, SOMEIP_EVENT_ID, ElementsAre( SOMEIP_EVENTGROUP_ID ), _, _ ) )
            .Times( 1 );
        EXPECT_CALL( *mSomeipApplicationMock,
                     subscribe( SOMEIP_SERVICE_ID, SOMEIP_INSTANCE_ID, SOMEIP_EVENTGROUP_ID, _, _ ) )
            .Times( 1 );
        EXPECT_CALL( *mSomeipApplicationMock, stop() ).Times( 1 );
        EXPECT_CALL( *mSomeipApplicationMock, start() ).Times( 1 );
    }

    void
    setupCanMessage( uint32_t canId, Timestamp timestamp, std::vector<uint8_t> payload )
    {
        std::vector<uint8_t> data = { static_cast<uint8_t>( canId >> 24 ),
                                      static_cast<uint8_t>( canId >> 16 ),
                                      static_cast<uint8_t>( canId >> 8 ),
                                      static_cast<uint8_t>( canId ),
                                      static_cast<uint8_t>( timestamp >> 56 ),
                                      static_cast<uint8_t>( timestamp >> 48 ),
                                      static_cast<uint8_t>( timestamp >> 40 ),
                                      static_cast<uint8_t>( timestamp >> 32 ),
                                      static_cast<uint8_t>( timestamp >> 24 ),
                                      static_cast<uint8_t>( timestamp >> 16 ),
                                      static_cast<uint8_t>( timestamp >> 8 ),
                                      static_cast<uint8_t>( timestamp ) };
        data.insert( data.end(), payload.begin(), payload.end() );
        mMessage->get_payload()->set_data( std::move( data ) );
    }

    void
    checkValidMessage( uint64_t expectedTimestamp )
    {
        CollectedDataFrame collectedDataFrame;
        ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
        auto signal = collectedDataFrame.mCollectedSignals[0];
        ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
        ASSERT_EQ( signal.signalID, 1 );
        if ( expectedTimestamp == 0 )
        {
            ASSERT_GT( signal.receiveTime, 0 );
        }
        else
        {
            ASSERT_EQ( signal.receiveTime, expectedTimestamp );
        }
        ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x10203 );
        signal = collectedDataFrame.mCollectedSignals[1];
        ASSERT_EQ( signal.signalID, 7 );
        ASSERT_EQ( signal.value.type, SignalType::DOUBLE );
        if ( expectedTimestamp == 0 )
        {
            ASSERT_GT( signal.receiveTime, 0 );
        }
        else
        {
            ASSERT_EQ( signal.receiveTime, expectedTimestamp );
        }
        ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, 0x4050607 );
    }

    SignalBufferPtr mSignalBuffer;
    SignalBufferDistributor mSignalBufferDistributor;
    CANDataConsumer mCanConsumer{ mSignalBufferDistributor };
    std::shared_ptr<StrictMock<SomeipApplicationMock>> mSomeipApplicationMock;
    SomeipToCanBridge mSomeipToCanBridge;
    vsomeip::message_handler_t mMessageHandler;
    vsomeip::sd_acceptance_handler_t mServiceDiscoveryAcceptanceHandler;
    vsomeip::availability_handler_t mAvailabilityHandler;
    std::shared_ptr<vsomeip::message> mMessage;
    std::shared_ptr<CANDecoderDictionary> mDictionary;
};

TEST_F( SomeipToCanBridgeTest, initFail )
{
    EXPECT_CALL( *mSomeipApplicationMock, init() ).Times( 1 ).WillRepeatedly( Return( false ) );
    ASSERT_FALSE( mSomeipToCanBridge.connect() );
}

TEST_F( SomeipToCanBridgeTest, initSuccess )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    vsomeip::remote_info_t remoteInfo{};
    ASSERT_TRUE( mServiceDiscoveryAcceptanceHandler( remoteInfo ) );
    mAvailabilityHandler( SOMEIP_SERVICE_ID, SOMEIP_INSTANCE_ID, true );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageTooShort )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mMessage->get_payload()->set_data( std::vector<uint8_t>() );
    mMessageHandler( mMessage );
    ASSERT_TRUE( mSignalBuffer->isEmpty() );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageNoDm )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mMessageHandler( mMessage );
    ASSERT_TRUE( mSignalBuffer->isEmpty() );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageNullDm )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mSomeipToCanBridge.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::RAW_SOCKET );
    mMessageHandler( mMessage );
    ASSERT_TRUE( mSignalBuffer->isEmpty() );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageOtherDm )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mSomeipToCanBridge.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::INVALID_PROTOCOL );
    mMessageHandler( mMessage );
    ASSERT_TRUE( mSignalBuffer->isEmpty() );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageValidDm )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mSomeipToCanBridge.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    mMessageHandler( mMessage );
    ASSERT_NO_FATAL_FAILURE( checkValidMessage( CAN_RECEIVE_TIME / 1000 ) );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageNoTimestamp )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mSomeipToCanBridge.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    setupCanMessage( CAN_ID, 0, { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 } );
    mMessageHandler( mMessage );
    ASSERT_NO_FATAL_FAILURE( checkValidMessage( 0 ) );
}

TEST_F( SomeipToCanBridgeTest, receiveMessageNonMonotonic )
{
    setupInitExpectations();
    ASSERT_TRUE( mSomeipToCanBridge.connect() );
    mSomeipToCanBridge.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    mMessageHandler( mMessage );
    ASSERT_NO_FATAL_FAILURE( checkValidMessage( CAN_RECEIVE_TIME / 1000 ) );
    setupCanMessage( CAN_ID, CAN_RECEIVE_TIME - 1000, { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 } );
    mMessageHandler( mMessage );
    ASSERT_NO_FATAL_FAILURE( checkValidMessage( ( CAN_RECEIVE_TIME - 1000 ) / 1000 ) );
}

} // namespace IoTFleetWise
} // namespace Aws
