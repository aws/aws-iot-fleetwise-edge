// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomDataSource.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include <chrono>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Between;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;

class CustomDataSourceTestImplementation : public CustomDataSource
{
public:
    MOCK_METHOD( (void), pollData, () );

    void
    setPollInternalIntervalMs( uint32_t pollIntervalMs )
    {
        setPollIntervalMs( pollIntervalMs );
    }
};

class CustomDataSourceTest : public ::testing::Test
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

/** @brief  pollData should be only called when at least one campaign uses a signal*/
TEST_F( CustomDataSourceTest, pollOnlyWithValidData )
{
    NiceMock<CustomDataSourceTestImplementation> implementationMock;
    EXPECT_CALL( implementationMock, pollData() ).Times( 0 );
    // no setFilter at
    implementationMock.start();
    implementationMock.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    implementationMock.setFilter( 5, 2 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    implementationMock.setFilter( 1, 2 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    implementationMock.setFilter( 2, 1 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

    // set the filter matching to dictionary 1,1 so now pollData should be called
    EXPECT_CALL( implementationMock, pollData() ).Times( AtLeast( 3 ) );
    implementationMock.setFilter( 1, 1 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // change filter again so calls should stop
    implementationMock.setFilter( 2, 1 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );
    EXPECT_CALL( implementationMock, pollData() ).Times( 0 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
}

TEST_F( CustomDataSourceTest, pollIntervalChanges )
{
    NiceMock<CustomDataSourceTestImplementation> implementationMock;
    EXPECT_CALL( implementationMock, pollData() ).Times( 0 );
    // no setFilter at
    implementationMock.setPollInternalIntervalMs( 50 );
    implementationMock.start();
    implementationMock.setFilter( 1, 1 );
    implementationMock.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );
    EXPECT_CALL( implementationMock, pollData() ).Times( Between( 8, 12 ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
}

} // namespace IoTFleetWise
} // namespace Aws
