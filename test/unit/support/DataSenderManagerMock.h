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
        : DataSenderManager( nullptr,
                             nullptr,
                             canIDTranslator,
                             0
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                             ,
                             nullptr,
                             nullptr,
                             ""
#endif
          )
    {
    }

    void
    processCollectedData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                          ,
                          std::function<void( TriggeredCollectionSchemeDataPtr )> reportUploadCallback
#endif
                          ) override
    {
        std::lock_guard<std::mutex> lock( mProcessedDataMutex );
        mProcessedData.push_back( triggeredCollectionSchemeDataPtr );
        mockedProcessCollectedData( triggeredCollectionSchemeDataPtr
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                    ,
                                    reportUploadCallback
#endif
        );
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    MOCK_METHOD( void,
                 mockedProcessCollectedData,
                 ( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr,
                   std::function<void( TriggeredCollectionSchemeDataPtr )> reportUploadCallback ) );
#else
    MOCK_METHOD( void,
                 mockedProcessCollectedData,
                 ( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr ) );
#endif

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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::shared_ptr<const ActiveCollectionSchemes> mActiveCollectionSchemes;
    void
    onChangeCollectionSchemeList(
        const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes ) override
    {
        mActiveCollectionSchemes = activeCollectionSchemes;
    }
#endif

private:
    // Record the calls so that we can wait for asynchronous calls to happen.
    std::vector<TriggeredCollectionSchemeDataPtr> mProcessedData;
    std::mutex mProcessedDataMutex;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
