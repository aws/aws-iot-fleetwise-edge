// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MqttClientWrapper.h"
#include <gmock/gmock.h>

namespace Aws
{
namespace IoTFleetWise
{

class MqttClientWrapperMock : public MqttClientWrapper
{
public:
    MqttClientWrapperMock()
        : MqttClientWrapper( nullptr ){};

    MOCK_METHOD( std::shared_ptr<MqttConnectionWrapper>,
                 NewConnection,
                 (const Aws::Iot::MqttClientConnectionConfig &),
                 ( noexcept ) );

    MOCK_METHOD( int, LastError, (), ( const, noexcept ) );

    MOCK_METHOD( bool, MockedOperatorBool, (), ( const, noexcept ) );

    virtual explicit operator bool() const noexcept override
    {
        return MockedOperatorBool();
    }
};

} // namespace IoTFleetWise
} // namespace Aws
