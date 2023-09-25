// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RemoteProfiler.h"
#include "IConnectionTypes.h"
#include "ISender.h"
#include "LogLevel.h"
#include "SenderMock.h"
#include "WaitUntil.h"
#include <atomic>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Gt;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::StrictMock;

void
checkMetrics( const std::string &data )
{
    Json::Reader reader;
    Json::Value root;
    ASSERT_TRUE( reader.parse( data, root ) );
    ASSERT_GE( data.size(), 6 );
    ASSERT_GE( root["metric1"]["name"].asString().length(), 1 );
}

std::atomic<int> a( 0 );
TEST( RemoteProfilerTest, MetricsUpload )
{
    auto senderMock = std::make_shared<StrictMock<Testing::SenderMock>>();
    EXPECT_CALL( *senderMock, mockedSendBuffer( _, Gt( 0 ), _ ) )
        .WillRepeatedly( Return( ConnectivityError::Success ) );

    auto mockLogSender = std::make_shared<Testing::SenderMock>();
    RemoteProfiler profiler( senderMock, mockLogSender, 1000, 1000, LogLevel::Trace, "Test" );
    profiler.start();

    // Generate some cpu load
    for ( int i = 0; i < 200000000; i++ )
    {
        a++;
    }

    WAIT_ASSERT_GT( senderMock->getSentBufferData().size(), 5U );
    for ( auto sentData : senderMock->getSentBufferData() )
    {
        ASSERT_NO_FATAL_FAILURE( checkMetrics( sentData.data ) );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
