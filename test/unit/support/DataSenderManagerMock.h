// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataSenderManager.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <mutex>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class DataSenderManagerMock : public DataSenderManager
{
public:
    unsigned mCheckAndSendRetrievedDataCalls{ 0 };

    DataSenderManagerMock()
        : DataSenderManager( {}, nullptr, nullptr )
    {
    }

    void
    processData( std::shared_ptr<const DataToSend> data ) override
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        mProcessedData.push_back( data );
        mockedProcessData( data );
    }

    MOCK_METHOD( void, mockedProcessData, (std::shared_ptr<const DataToSend>));

    std::vector<std::shared_ptr<const DataToSend>>
    getProcessedData()
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        return mProcessedData;
    }

    template <typename T>
    std::vector<std::shared_ptr<const T>>
    getProcessedData()
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        std::vector<std::shared_ptr<const T>> castedProcessedData;
        for ( auto data : mProcessedData )
        {
            castedProcessedData.push_back( std::dynamic_pointer_cast<const T>( data ) );
        }
        return castedProcessedData;
    }

    void
    checkAndSendRetrievedData() override
    {
        mCheckAndSendRetrievedDataCalls++;
    }

private:
    // Record the calls so that we can wait for asynchronous calls to happen.
    std::vector<std::shared_ptr<const DataToSend>> mProcessedData;
    std::mutex mProcessedDataMutex;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
