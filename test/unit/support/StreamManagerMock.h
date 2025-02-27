// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/snf/StreamManager.h"
#include <gmock/gmock.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class StreamManagerMock : public Aws::IoTFleetWise::Store::StreamManager
{
public:
    explicit StreamManagerMock( std::unique_ptr<DataSenderProtoWriter> protoWriter )
        : StreamManager( "", std::move( protoWriter ), 0 ){};

    MOCK_METHOD( Store::StreamManager::ReturnCode, appendToStreams, ( const TriggeredCollectionSchemeData &data ) );

    MOCK_METHOD( bool, hasCampaign, ( const Aws::IoTFleetWise::Store::CampaignName &campaignName ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
