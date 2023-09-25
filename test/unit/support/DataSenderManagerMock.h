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

    DataSenderManagerMock( CANInterfaceIDTranslator &canIDTranslator )
        : DataSenderManager( nullptr, nullptr, canIDTranslator, 0 )
    {
    }

    void
    processCollectedData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr ) override
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        mProcessedData.push_back( triggeredCollectionSchemeDataPtr );
        mockedProcessCollectedData( triggeredCollectionSchemeDataPtr );
    }

    MOCK_METHOD( void,
                 mockedProcessCollectedData,
                 ( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr ) );

    std::vector<TriggeredCollectionSchemeDataPtr>
    getProcessedData()
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        return mProcessedData;
    }

    void
    checkAndSendRetrievedData() override
    {
        mCheckAndSendRetrievedDataCalls++;
    }

private:
    // Record the calls so that we can wait for asynchronous calls to happen.
    std::vector<TriggeredCollectionSchemeDataPtr> mProcessedData;
    std::mutex mProcessedDataMutex;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
