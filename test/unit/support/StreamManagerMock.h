// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/snf/StreamManager.h"
#include <gmock/gmock.h>
#include <memory>
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
    explicit StreamManagerMock()
        : StreamManager( "" ){};

    MOCK_METHOD( Store::StreamManager::ReturnCode,
                 appendToStreams,
                 ( const TelemetryDataToPersist &data ),
                 ( override ) );

    MOCK_METHOD( bool, hasCampaign, ( const Aws::IoTFleetWise::Store::CampaignName &campaignName ), ( override ) );

    MOCK_METHOD( std::shared_ptr<const std::vector<Store::Partition>>,
                 getPartitions,
                 ( const std::string &campaignArn ),
                 ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
