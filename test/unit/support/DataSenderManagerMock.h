// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/DataSenderManager.h"
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
        : DataSenderManager( {}, nullptr )
    {
    }

    void
    processData( const DataToSend &data ) override
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        mProcessedData.push_back( &data );
        mockedProcessData( data );
    }

    MOCK_METHOD( void, mockedProcessData, (const DataToSend &));

    std::vector<const DataToSend *>
    getProcessedData()
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        return mProcessedData;
    }

    template <typename T>
    std::vector<const T *>
    getProcessedData()
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        std::vector<const T *> castedProcessedData;
        for ( auto data : mProcessedData )
        {
            castedProcessedData.push_back( dynamic_cast<const T *>( data ) );
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
    std::vector<const DataToSend *> mProcessedData;
    std::mutex mProcessedDataMutex;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
