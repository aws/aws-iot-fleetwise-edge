// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExternalGpsSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "NamedSignalDataSource.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

class ExternalGpsSourceTest : public ::testing::Test
{
protected:
    ExternalGpsSourceTest()
        : mSignalBuffer( std::make_shared<SignalBuffer>( 2, "Signal Buffer" ) )
        , mSignalBufferDistributor( std::make_shared<SignalBufferDistributor>() )
        , mNamedSignalDataSource( std::make_shared<NamedSignalDataSource>( "5", mSignalBufferDistributor ) )
        , mExternalGpsSource( std::make_shared<ExternalGpsSource>(
              mNamedSignalDataSource, "Vehicle.CurrentLocation.Latitude", "Vehicle.CurrentLocation.Longitude" ) )
        , mDictionary( std::make_shared<CustomDecoderDictionary>() )
    {
        mSignalBufferDistributor->registerQueue( mSignalBuffer );
    }

    void
    SetUp() override
    {
        mDictionary->customDecoderMethod["5"]["Vehicle.CurrentLocation.Latitude"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.CurrentLocation.Latitude", 0x1234, SignalType::DOUBLE };
        mDictionary->customDecoderMethod["5"]["Vehicle.CurrentLocation.Longitude"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.CurrentLocation.Longitude", 0x5678, SignalType::DOUBLE };
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<SignalBuffer> mSignalBuffer;
    SignalBufferDistributorPtr mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<ExternalGpsSource> mExternalGpsSource;
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
};

// Test if valid gps data
TEST_F( ExternalGpsSourceTest, testDecoding ) // NOLINT
{
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    CollectedDataFrame collectedDataFrame;
    DELAY_ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );

    mExternalGpsSource->setLocation( 360, 360 ); // Invalid
    mExternalGpsSource->setLocation( 52.5761, 12.5761 );

    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );

    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    auto secondSignal = collectedDataFrame.mCollectedSignals[1];

    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, 12.5761, 0.0001 );
}

// Test longitude west
TEST_F( ExternalGpsSourceTest, testWestNegativeLongitude ) // NOLINT
{
    CollectedDataFrame collectedDataFrame;
    DELAY_ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    DELAY_ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );
    mExternalGpsSource->setLocation( 52.5761, -12.5761 );

    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    auto secondSignal = collectedDataFrame.mCollectedSignals[1];
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, -12.5761, 0.0001 ); // negative number
}

} // namespace IoTFleetWise
} // namespace Aws
