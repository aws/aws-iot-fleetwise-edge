
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

#include "IoTFleetWiseEngine.h"
#include "IoTFleetWiseConfig.h"
#include "LogLevel.h"
#include <gtest/gtest.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/isotp.h>
#include <sys/socket.h>

using namespace Aws::IoTFleetWise::ExecutionManagement;
using namespace Aws::IoTFleetWise::DataManagement;

namespace
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
} // namespace

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
    }
};

TEST_F( IoTFleetWiseEngineTest, InitAndStartEngine )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "em-example-config.json", config ) );
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
    ASSERT_TRUE( IoTFleetWiseConfig::read( "em-example-config.json", config ) );
    IoTFleetWiseEngine engine;
    ASSERT_TRUE( engine.connect( config ) );

    // Push to the publish data queue
    std::shared_ptr<TriggeredCollectionSchemeData> collectedDataPtr = std::make_shared<TriggeredCollectionSchemeData>();
    collectedDataPtr->metaData.collectionSchemeID = "123";
    collectedDataPtr->metaData.decoderID = "456";
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
        std::array<uint8_t, 8> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
        CollectedCanRawFrame canFrames1( 12 /*frameId*/, 1 /*nodeId*/, 815 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames1 );
        CollectedCanRawFrame canFrames2( 4 /*frameId*/, 2 /*nodeId*/, 1100 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames2 );
        CollectedCanRawFrame canFrames3( 6 /*frameId*/, 3 /*nodeId*/, 1300 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames3 );
    }

    ASSERT_TRUE( engine.mCollectedDataReadyToPublish->push( collectedDataPtr ) );

    ASSERT_TRUE( engine.start() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    ASSERT_TRUE( engine.isAlive() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, InitAndFailToStartCorruptConfig )
{
    Json::Value config;
    // Read should succeed
    ASSERT_TRUE( IoTFleetWiseConfig::read( "em-example-config-corrupt.json", config ) );
    IoTFleetWiseEngine engine;
    // Connect should fail as the Config file has a non complete Bus definition
    ASSERT_FALSE( engine.connect( config ) );
}

TEST_F( IoTFleetWiseEngineTest, TestDataRetrieval )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "em-example-config.json", config ) );
    IoTFleetWiseEngine engine;
    ASSERT_TRUE( engine.connect( config ) );
    // Write some data to the file
    std::string testString = "!24$iklmnopabcdefjh!24$3@qrstuvwxyz";

    ASSERT_EQ( engine.mPersistDecoderManifestCollectionSchemesAndData->write(
                   reinterpret_cast<const uint8_t *>( testString.c_str() ),
                   testString.size(),
                   DataType::EDGE_TO_CLOUD_PAYLOAD ),
               ErrorCode::SUCCESS );

    ASSERT_TRUE( engine.start() );
    ASSERT_TRUE( engine.isAlive() );
    ASSERT_EQ( engine.mPersistDecoderManifestCollectionSchemesAndData->erase( DataType::EDGE_TO_CLOUD_PAYLOAD ),
               ErrorCode::SUCCESS );
    ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}
