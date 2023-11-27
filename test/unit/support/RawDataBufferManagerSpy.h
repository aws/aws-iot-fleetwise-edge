// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "RawDataManager.h"
#include <gmock/gmock.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class RawDataBufferManagerSpy : public RawData::BufferManager
{
public:
    RawDataBufferManagerSpy( const RawData::BufferManagerConfig &config )
        : RawData::BufferManager( config ){};

    bool
    increaseHandleUsageHint( RawData::BufferTypeId typeId,
                             RawData::BufferHandle handle,
                             RawData::BufferHandleUsageStage handleUsageStage ) override
    {
        // Call the mock method just to allow tests to spy on it. But call the original method too.
        mockedIncreaseHandleUsageHint( typeId, handle, handleUsageStage );
        return RawData::BufferManager::increaseHandleUsageHint( typeId, handle, handleUsageStage );
    }

    MOCK_METHOD( void,
                 mockedIncreaseHandleUsageHint,
                 ( RawData::BufferTypeId typeId,
                   RawData::BufferHandle handle,
                   RawData::BufferHandleUsageStage handleUsageStage ) );

    bool
    decreaseHandleUsageHint( RawData::BufferTypeId typeId,
                             RawData::BufferHandle handle,
                             RawData::BufferHandleUsageStage handleUsageStage ) override
    {
        // Call the mock method just to allow tests to spy on it. But call the original method too.
        mockedDecreaseHandleUsageHint( typeId, handle, handleUsageStage );
        return RawData::BufferManager::decreaseHandleUsageHint( typeId, handle, handleUsageStage );
    }

    MOCK_METHOD( void,
                 mockedDecreaseHandleUsageHint,
                 ( RawData::BufferTypeId typeId,
                   RawData::BufferHandle handle,
                   RawData::BufferHandleUsageStage handleUsageStage ) );

    RawData::BufferErrorCode
    updateConfig( const std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
    {
        mockedUpdateConfig( updatedSignals );
        return RawData::BufferManager::updateConfig( updatedSignals );
    }

    MOCK_METHOD( RawData::BufferErrorCode,
                 mockedUpdateConfig,
                 ( ( const std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals ) ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
