// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StreamManagerMock.h"
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include <gmock/gmock.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class StreamForwarderMock : public Aws::IoTFleetWise::Store::StreamForwarder
{
public:
    explicit StreamForwarderMock( StreamManagerMock &streamManager,
                                  TelemetryDataSender &dataSender,
                                  RateLimiter &rateLimiter )
        : StreamForwarder( streamManager, dataSender, rateLimiter ){};

    MOCK_METHOD( void, registerJobCompletionCallback, ( JobCompletionCallback callback ), ( override ) );

    MOCK_METHOD( void,
                 beginForward,
                 ( const Aws::IoTFleetWise::Store::CampaignName &campaignID,
                   Aws::IoTFleetWise::Store::PartitionID pID,
                   Aws::IoTFleetWise::Store::StreamForwarder::Source source ),
                 ( override ) );

    MOCK_METHOD( void,
                 cancelForward,
                 ( const Aws::IoTFleetWise::Store::CampaignName &campaignID,
                   Aws::IoTFleetWise::Store::PartitionID pID,
                   Aws::IoTFleetWise::Store::StreamForwarder::Source source ),
                 ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
