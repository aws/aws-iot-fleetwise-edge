// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StreamForwarder.h"
#include "StreamManagerMock.h"
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
    explicit StreamForwarderMock( std::shared_ptr<StreamManagerMock> streamManager,
                                  std::shared_ptr<TelemetryDataSender> dataSender )
        : StreamForwarder( streamManager, dataSender, nullptr ){};

    MOCK_METHOD( void, registerJobCompletionCallback, ( JobCompletionCallback callback ), ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
