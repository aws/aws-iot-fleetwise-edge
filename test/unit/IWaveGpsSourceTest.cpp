// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IWaveGpsSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <boost/lockfree/queue.hpp>
#include <climits>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class IWaveGpsSourceTest : public ::testing::Test
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
        char bufferFilePath[PATH_MAX];
        if ( getcwd( bufferFilePath, sizeof( bufferFilePath ) ) != nullptr )
        {
            filePath = bufferFilePath;
            filePath += "/testGpsNMEA.txt";
            std::cout << " File being saved here: " << filePath << std::endl;
            nmeaFile = std::make_unique<std::ofstream>( filePath );
        }
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<CANDecoderDictionary> mDictionary;
    std::unique_ptr<std::ofstream> nmeaFile;
    std::string filePath;
};

// Test if valid gps data
TEST_F( IWaveGpsSourceTest, testDecoding )
{
    SignalBufferPtr signalBufferPtr = std::make_shared<SignalBuffer>( 100 );
    // Random data so checksum etc. will not be valid
    *nmeaFile << "$GPGSV,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25*26\n"
                 "GPGSV,27,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25*26\\n\n"
                 "GPGSV,28,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25*26\\n\n"
                 "$GPGGA,133120.00,5234.56789,N,01234.56789,E,1,08,0.6,123.4,M,56.7,M,,*89\n\n"
                 "$GPVTG,29.30,T,31.32,M,33.34,N,35.36,K,A*37C\n\n\n";
    nmeaFile->close();
    IWaveGpsSource gpsSource( signalBufferPtr );
    ASSERT_FALSE( gpsSource.init( filePath, INVALID_CAN_SOURCE_NUMERIC_ID, 1, 0, 32 ) );
    ASSERT_TRUE( gpsSource.init( filePath, 1, 1, 0, 32 ) );
    gpsSource.connect();
    gpsSource.start();
    CollectedSignal firstSignal;
    CollectedSignal secondSignal;
    DELAY_ASSERT_FALSE( signalBufferPtr->pop( firstSignal ) );
    gpsSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );

    WAIT_ASSERT_TRUE( signalBufferPtr->pop( firstSignal ) );
    ASSERT_TRUE( signalBufferPtr->pop( secondSignal ) );
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    // raw value from NMEA 5234.56789 01234.56789 converted to DD
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, 12.5761, 0.0001 );

    ASSERT_TRUE( gpsSource.stop() );
    ASSERT_TRUE( gpsSource.disconnect() );
}

// Test longitude west
TEST_F( IWaveGpsSourceTest, testWestNegativeLongitude )
{
    SignalBufferPtr signalBufferPtr = std::make_shared<SignalBuffer>( 100 );
    // instead of E for east now, W for West
    *nmeaFile << "$GPGGA,133120.00,5234.56789,N,01234.56789,W,1,08,0.6,123.4,M,56.7,M,,*89\n\n";
    nmeaFile->close();
    IWaveGpsSource gpsSource( signalBufferPtr );
    gpsSource.init( filePath, 1, 1, 0, 32 );
    gpsSource.connect();
    gpsSource.start();
    gpsSource.onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );

    CollectedSignal firstSignal;
    CollectedSignal secondSignal;
    WAIT_ASSERT_TRUE( signalBufferPtr->pop( firstSignal ) );
    ASSERT_TRUE( signalBufferPtr->pop( secondSignal ) );
    ASSERT_EQ( firstSignal.signalID, 0x1234 );
    ASSERT_EQ( secondSignal.signalID, 0x5678 );
    // raw value from NMEA 5234.56789 01234.56789 converted to DD
    ASSERT_NEAR( firstSignal.value.value.doubleVal, 52.5761, 0.0001 );
    ASSERT_NEAR( secondSignal.value.value.doubleVal, -12.5761, 0.0001 ); // negative number

    ASSERT_TRUE( gpsSource.stop() );
    ASSERT_TRUE( gpsSource.disconnect() );
}

} // namespace IoTFleetWise
} // namespace Aws
