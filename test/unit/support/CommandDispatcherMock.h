// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICommandDispatcher.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <mutex>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class CommandDispatcherMock : public ICommandDispatcher
{
public:
    CommandDispatcherMock()
        : ICommandDispatcher()
    {
    }

    MOCK_METHOD( bool, init, () );
    MOCK_METHOD( void,
                 setActuatorValue,
                 ( const std::string &actuatorName,
                   const SignalValueWrapper &signalValue,
                   const CommandID &commandId,
                   Timestamp issuedTimestampMs,
                   Timestamp executionTimeoutMs,
                   NotifyCommandStatusCallback notifyStatusCallback ) );
    MOCK_METHOD( std::vector<std::string>, getActuatorNames, () );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
