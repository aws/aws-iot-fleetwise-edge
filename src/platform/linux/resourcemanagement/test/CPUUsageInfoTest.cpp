
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

#include "CPUUsageInfo.h"

#include <Thread.h>
#include <gtest/gtest.h>
#include <iostream>

using namespace Aws::IoTFleetWise::Platform;

TEST( CPUUsageInfoTest, testCPUUsageInfo )
{
    CPUUsageInfo cpuUsage;

    ASSERT_TRUE( cpuUsage.reportCPUUsageInfo() );
    ASSERT_GE( cpuUsage.getKernelSpaceTime(), 0 );
    CPUUsageInfo::ThreadCPUUsageInfos infos;
    Thread::SetCurrentThreadName( "MainCPURTest" );

    std::thread busyThread( []() {
        Thread::SetCurrentThreadName( "BusyThread" );
        auto startTime = std::chrono::system_clock::now();
        // busy loop for around one second
        for ( volatile uint64_t i = 0; i < 200000000000; i++ )
        {
            auto now = std::chrono::system_clock::now();
            auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>( now - startTime );
            if ( milliseconds.count() > 1000 )
            {
                break;
            }
        };
        std::this_thread::sleep_for( std::chrono::milliseconds( 800 ) );
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    ASSERT_TRUE( cpuUsage.reportPerThreadUsageData( infos ) );
    ASSERT_EQ( infos.size(), 2 );
    for ( auto t : infos )
    {
        if ( t.threadName == "BusyThread" )
        {
            ASSERT_GE( t.mUserSpaceTime,
                       0.1 ); // If in the 1 seconds busy run our process got only 0.1 seconds than os is very loaded.
        }
        std::cout << "Thread " << t.threadId << "(" << t.threadName << ") user time: " << t.mUserSpaceTime
                  << " system time: " << t.mKernelSpaceTime << std::endl;
    }
    busyThread.join();
}
