// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IWaveGpsSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "NamedSignalDataSource.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <climits>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

class IWaveGpsSourceTest : public ::testing::Test
{
protected:
    IWaveGpsSourceTest()
        : mSignalBuffer( std::make_shared<SignalBuffer>( 2, "Signal Buffer" ) )
        , mSignalBufferDistributor( std::make_shared<SignalBufferDistributor>() )
        , mNamedSignalDataSource( std::make_shared<NamedSignalDataSource>( "5", mSignalBufferDistributor ) )
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

        char bufferFilePath[PATH_MAX];
        if ( getcwd( bufferFilePath, sizeof( bufferFilePath ) ) != nullptr )
        {
            mFilePath = bufferFilePath;
            mFilePath += "/testGpsNMEA.txt";
            std::cout << "File being saved here: " << mFilePath << std::endl;
            mNmeaFile = std::make_unique<std::ofstream>( mFilePath );
            mIWaveGpsSource = std::make_shared<IWaveGpsSource>( mNamedSignalDataSource,
                                                                mFilePath,
                                                                "Vehicle.CurrentLocation.Latitude",
                                                                "Vehicle.CurrentLocation.Longitude",
                                                                1000 );
        }
    }

    void
    TearDown() override
    {
    }

    std::string mFilePath;
    std::unique_ptr<std::ofstream> mNmeaFile;
    std::shared_ptr<SignalBuffer> mSignalBuffer;
    SignalBufferDistributorPtr mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<IWaveGpsSource> mIWaveGpsSource;
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
};

// Test if valid gps data
TEST_F( IWaveGpsSourceTest, testDecoding )
{
    ASSERT_TRUE( mIWaveGpsSource->connect() );

    CollectedDataFrame collectedDataFrame;
    DELAY_ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    // Random data so checksum etc. will not be valid
    *mNmeaFile << "$GPGSV,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25*26\n"
                  "GPGSV,27,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25*26\\n\n"
                  "GPGSV,28,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25*26\\n\n"
                  "$GPGGA,133120.00,5234.56789,N,01234.56789,E,1,08,0.6,123.4,M,56.7,M,,*89\n\n"
                  "$GPVTG,29.30,T,31.32,M,33.34,N,35.36,K,A*37C\n\n\n";
    mNmeaFile->close();

    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );

    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    auto secondSignal = collectedDataFrame.mCollectedSignals[1];

    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    // raw value from NMEA 5234.56789 01234.56789 converted to DD
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, 12.5761, 0.0001 );
}

// Test longitude west
TEST_F( IWaveGpsSourceTest, testWestNegativeLongitude )
{
    ASSERT_TRUE( mIWaveGpsSource->connect() );
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    // instead of E for east now, W for West
    *mNmeaFile << "$GPGGA,133120.00,5234.56789,N,01234.56789,W,1,08,0.6,123.4,M,56.7,M,,*89\n\n";
    mNmeaFile->close();

    CollectedDataFrame collectedDataFrame;
    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );

    auto firstSignal = collectedDataFrame.mCollectedSignals[0];
    auto secondSignal = collectedDataFrame.mCollectedSignals[1];

    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    // raw value from NMEA 5234.56789 01234.56789 converted to DD
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, -12.5761, 0.0001 ); // negative number
}

} // namespace IoTFleetWise
} // namespace Aws
