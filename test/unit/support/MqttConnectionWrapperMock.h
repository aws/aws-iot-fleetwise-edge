// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MqttConnectionWrapper.h"
#include <gmock/gmock.h>

namespace Aws
{
namespace IoTFleetWise
{

class MqttConnectionWrapperMock : public MqttConnectionWrapper
{
public:
    MqttConnectionWrapperMock()
        : MqttConnectionWrapper( nullptr ){};

    MOCK_METHOD( bool, MockedOperatorBool, (), ( const, noexcept ) );

    virtual explicit operator bool() const noexcept override
    {
        return MockedOperatorBool();
    }

    MOCK_METHOD( int, LastError, (), ( const, noexcept ) );
    MOCK_METHOD( bool, Connect, ( const char *, bool, uint16_t, uint32_t ), ( noexcept ) );
    MOCK_METHOD( bool, Disconnect, (), ( noexcept ) );
    MOCK_METHOD( uint16_t,
                 Subscribe,
                 (const char *, Aws::Crt::Mqtt::QOS, OnMessageReceivedHandler &&, OnSubAckHandler &&),
                 ( noexcept ) );
    MOCK_METHOD( bool, SetOnMessageHandler, ( OnMessageReceivedHandler && ), ( noexcept ) );
    MOCK_METHOD( uint16_t, Unsubscribe, (const char *, OnOperationCompleteHandler &&), ( noexcept ) );
    MOCK_METHOD( uint16_t,
                 Publish,
                 (const char *, Aws::Crt::Mqtt::QOS, bool, const Aws::Crt::ByteBuf &, OnOperationCompleteHandler &&),
                 ( noexcept ) );

    virtual void
    SetOnConnectionInterrupted( OnConnectionInterruptedHandler onConnectionInterrupted )
    {
        mOnConnectionInterrupted = std::move( onConnectionInterrupted );
    }
    virtual void
    SetOnConnectionResumed( OnConnectionResumedHandler onConnectionResumed )
    {
        mOnConnectionResumed = std::move( onConnectionResumed );
    }
    virtual void
    SetOnConnectionCompleted( OnConnectionCompletedHandler onConnectionCompleted )
    {
        mOnConnectionCompleted = std::move( onConnectionCompleted );
    }
    virtual void
    SetOnDisconnect( OnDisconnectHandler onDisconnect )
    {
        mOnDisconnect = std::move( onDisconnect );
    }

    OnConnectionInterruptedHandler mOnConnectionInterrupted;
    OnConnectionResumedHandler mOnConnectionResumed;
    OnConnectionCompletedHandler mOnConnectionCompleted;
    OnDisconnectHandler mOnDisconnect;
};

} // namespace IoTFleetWise
} // namespace Aws
