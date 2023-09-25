
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CPUUsageInfo.h"
#include <Thread.h>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

TEST( CPUUsageInfoTest, testCPUUsageInfo )
{
    CPUUsageInfo cpuUsage;

    ASSERT_TRUE( cpuUsage.reportCPUUsageInfo() );
    ASSERT_GE( cpuUsage.getKernelSpaceTime(), 0 );
    CPUUsageInfo::ThreadCPUUsageInfos infos;
    Thread::setCurrentThreadName( "MainCPURTest" );

    std::thread busyThread( []() {
        Thread::setCurrentThreadName( "BusyThread" );
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

} // namespace IoTFleetWise
} // namespace Aws
