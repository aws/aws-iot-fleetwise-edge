// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "DataSenderIonWriter.h"
#include <gmock/gmock.h>
#include <memory>
#include <streambuf>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class DataSenderIonWriterMock : public DataSenderIonWriter
{
public:
    DataSenderIonWriterMock()
        : DataSenderIonWriter( nullptr, "" ){};

    // Record the calls so that we can wait for asynchronous calls to happen.
    std::vector<CollectedSignal> mSignals;

    MOCK_METHOD( void,
                 setupVehicleData,
                 ( std::shared_ptr<const TriggeredVisionSystemData> mTriggeredVisionSystemData ),
                 ( override ) );

    MOCK_METHOD( std::unique_ptr<StreambufBuilder>, getStreambufBuilder, (), ( override ) );
    void
    append( const CollectedSignal &signal ) override
    {
        mSignals.push_back( signal );
        mockedAppend( signal );
    };
    MOCK_METHOD( void, mockedAppend, ( const CollectedSignal &signal ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
