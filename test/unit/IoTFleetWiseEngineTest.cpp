
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IoTFleetWiseEngine.h"
#include "CANDataTypes.h"
#include "CollectionInspectionAPITypes.h"
#include "IoTFleetWiseConfig.h"
#include "WaitUntil.h"
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <json/json.h>
#include <linux/can.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#ifdef FWE_FEATURE_IWAVE_GPS
#include <fstream>
#endif

namespace Aws
{
namespace IoTFleetWise
{

bool
socketAvailable()
{
    auto sock = socket( PF_CAN, SOCK_DGRAM, CAN_ISOTP );
    if ( sock < 0 )
    {
        return false;
    }
    close( sock );
    return true;
}

class IoTFleetWiseEngineTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        if ( !socketAvailable() )
        {
            GTEST_SKIP() << "Skipping test fixture due to unavailability of socket";
        }
#ifdef FWE_FEATURE_IWAVE_GPS
        std::ofstream iWaveGpsFile( "/tmp/engineTestIWaveGPSfile.txt" );
        iWaveGpsFile << "NO valid NMEA data";
        iWaveGpsFile.close();
#endif
    }
};

TEST_F( IoTFleetWiseEngineTest, InitAndStartEngine )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-ok.json", config ) );
    IoTFleetWiseEngine engine;

    ASSERT_TRUE( engine.connect( config ) );

    ASSERT_TRUE( engine.start() );
    ASSERT_TRUE( engine.isAlive() );
    ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, InitAndStartEngineInlineCreds )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-inline-creds.json", config ) );
    IoTFleetWiseEngine engine;

    ASSERT_TRUE( engine.connect( config ) );

    ASSERT_TRUE( engine.start() );
    ASSERT_TRUE( engine.isAlive() );
    ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, CheckPublishDataQueue )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-ok.json", config ) );
    IoTFleetWiseEngine engine;
    ASSERT_TRUE( engine.connect( config ) );

    // Push to the publish data queue
    std::shared_ptr<TriggeredCollectionSchemeData> collectedDataPtr = std::make_shared<TriggeredCollectionSchemeData>();
    collectedDataPtr->metadata.collectionSchemeID = "123";
    collectedDataPtr->metadata.decoderID = "456";
    collectedDataPtr->triggerTime = 800;
    {
        CollectedSignal collectedSignalMsg1( 120 /*signalId*/, 800 /*receiveTime*/, 77.88 /*value*/ );
        collectedDataPtr->signals.push_back( collectedSignalMsg1 );
        CollectedSignal collectedSignalMsg2( 10 /*signalId*/, 1000 /*receiveTime*/, 46.5 /*value*/ );
        collectedDataPtr->signals.push_back( collectedSignalMsg2 );
        CollectedSignal collectedSignalMsg3( 12 /*signalId*/, 1200 /*receiveTime*/, 98.9 /*value*/ );
        collectedDataPtr->signals.push_back( collectedSignalMsg3 );
    }
    {
        std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
        CollectedCanRawFrame canFrames1( 12 /*frameId*/, 1 /*nodeId*/, 815 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames1 );
        CollectedCanRawFrame canFrames2( 4 /*frameId*/, 2 /*nodeId*/, 1100 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames2 );
        CollectedCanRawFrame canFrames3( 6 /*frameId*/, 3 /*nodeId*/, 1300 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames3 );
    }

    ASSERT_TRUE( engine.mCollectedDataReadyToPublish->push( collectedDataPtr ) );

    ASSERT_TRUE( engine.start() );

    WAIT_ASSERT_TRUE( engine.isAlive() );

    WAIT_ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, InitAndFailToStartCorruptConfig )
{
    Json::Value config;
    // Read should succeed
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-corrupt.json", config ) );
    IoTFleetWiseEngine engine;
    // Connect should fail as the Config file has a non complete Bus definition
    ASSERT_FALSE( engine.connect( config ) );
}

} // namespace IoTFleetWise
} // namespace Aws
