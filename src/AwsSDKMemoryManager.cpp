// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsSDKMemoryManager.h"
#include "TraceModule.h"
#include <cstddef>

namespace Aws
{
namespace IoTFleetWise
{

AwsSDKMemoryManager &
AwsSDKMemoryManager::getInstance()
{
    static AwsSDKMemoryManager instance;
    return instance;
}

std::size_t
AwsSDKMemoryManager::getLimit()
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mMaximumAwsSDKMemorySize;
}

bool
AwsSDKMemoryManager::setLimit( size_t size )
{
    std::lock_guard<std::mutex> lock( mMutex );
    if ( size == 0U )
    {
        return false;
    }
    mMaximumAwsSDKMemorySize = size;
    return true;
}

bool
AwsSDKMemoryManager::reserveMemory( std::size_t bytes )
{
    std::lock_guard<std::mutex> lock( mMutex );
    if ( ( mMemoryUsedAndReserved + bytes ) > mMaximumAwsSDKMemorySize )
    {
        return false;
    }
    mMemoryUsedAndReserved += bytes;
    TraceModule::get().setVariable( TraceVariable::MQTT_HEAP_USAGE, mMemoryUsedAndReserved );
    return true;
}

std::size_t
AwsSDKMemoryManager::releaseReservedMemory( std::size_t bytes )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mMemoryUsedAndReserved -= bytes;
    TraceModule::get().setVariable( TraceVariable::MQTT_HEAP_USAGE, mMemoryUsedAndReserved );
    return mMemoryUsedAndReserved;
}

} // namespace IoTFleetWise
} // namespace Aws
