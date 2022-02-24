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

#include "RemoteProfiler.h"
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <thread>

using namespace Aws::IoTFleetWise::OffboardConnectivity;

class MockSender : public ISender
{
public:
    // Create a callback that will allow the sender to inject a testing function
    using Callback = std::function<ConnectivityError( const std::uint8_t *buf, size_t size )>;
    Callback mCallback;

    bool
    isAlive() override
    {
        return true;
    }

    size_t
    getMaxSendSize() const override
    {
        return 12345;
    }

    ConnectivityError
    send( const std::uint8_t *buf,
          size_t size,
          struct Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams collectionSchemeParams =
              CollectionSchemeParams() ) override
    {
        static_cast<void>( collectionSchemeParams ); // Currently not implemented, hence unused
        std::cout << buf << std::endl;
        if ( !mCallback )
        {
            return ConnectivityError::NoConnection;
        }
        return mCallback( buf, size );
    }
};

void
checkMetrics( const std::uint8_t *buf, size_t size )
{
    Json::Reader reader;
    Json::Value root;
    ASSERT_TRUE( reader.parse( std::string( reinterpret_cast<const char *>( buf ) ), root ) );
    ASSERT_GE( size, 6 );
    ASSERT_GE( root["metric1"]["name"].asString().length(), 1 );
}

std::atomic<int> a( 0 );
TEST( RemoteProfilerTest, MetricsUpload )
{
    auto mockMetricsSender = std::make_shared<MockSender>();
    mockMetricsSender->mCallback = [&]( const std::uint8_t *buf, size_t size ) -> ConnectivityError {
        checkMetrics( buf, size );
        return ConnectivityError::Success;
    };
    auto mockLogSender = std::make_shared<MockSender>();
    RemoteProfiler profiler( mockMetricsSender, mockLogSender, 1000, 1000, LogLevel::Trace, "Test" );
    profiler.start();

    // Generate some cpu load
    for ( int i = 0; i < 200000000; i++ )
    {
        a++;
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 1500 ) );
}