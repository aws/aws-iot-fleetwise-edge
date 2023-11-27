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

    MOCK_METHOD( int, LastError, (), ( const, noexcept, override ) );

    MOCK_METHOD( bool, MockedOperatorBool, (), ( const noexcept ) );

    explicit operator bool() const noexcept override
    {
        return MockedOperatorBool();
    }

    MOCK_METHOD( bool, Start, (), ( const, noexcept, override ) );

    MOCK_METHOD( bool, Stop, (), ( noexcept, override ) );

    MOCK_METHOD( bool,
                 Stop,
                 ( std::shared_ptr<Aws::Crt::Mqtt5::DisconnectPacket> disconnectOptions ),
                 ( noexcept, override ) );

    MOCK_METHOD( bool,
                 Publish,
                 ( std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket> publishOptions,
                   Aws::Crt::Mqtt5::OnPublishCompletionHandler onPublishCompletionCallback ),
                 ( noexcept, override ) );

    MOCK_METHOD( bool,
                 Subscribe,
                 ( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket> subscribeOptions,
                   Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ),
                 ( noexcept, override ) );

    MOCK_METHOD( bool,
                 Unsubscribe,
                 ( std::shared_ptr<UnsubscribePacket> unsubscribeOptions,
                   Aws::Crt::Mqtt5::OnUnsubscribeCompletionHandler onUnsubscribeCompletionCallback ),
                 ( noexcept, override ) );

    MOCK_METHOD( const Aws::Crt::Mqtt5::Mqtt5ClientOperationStatistics &,
                 GetOperationStatistics,
                 (),
                 ( noexcept, override ) );
};

class MqttClientBuilderWrapperMock : public MqttClientBuilderWrapper
{
public:
    MqttClientBuilderWrapperMock()
        : MqttClientBuilderWrapper( nullptr ){};

    MOCK_METHOD( int, LastError, (), ( const, noexcept, override ) );

    MOCK_METHOD( bool, MockedOperatorBool, (), ( const noexcept ) );

    explicit operator bool() const noexcept override
    {
        return MockedOperatorBool();
    }

    MOCK_METHOD( std::shared_ptr<MqttClientWrapper>, Build, (), ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &, WithHostName, ( Crt::String hostname ), ( override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &, WithPort, ( uint16_t port ), ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithCertificateAuthority,
                 ( const Crt::ByteCursor &cert ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithHttpProxyOptions,
                 ( const Crt::Http::HttpClientConnectionProxyOptions &proxyOptions ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithCustomAuthorizer,
                 ( const Iot::Mqtt5CustomAuthConfig &config ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithConnectOptions,
                 ( std::shared_ptr<ConnectPacket> packetConnect ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithSessionBehavior,
                 ( ClientSessionBehaviorType sessionBehavior ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithClientExtendedValidationAndFlowControl,
                 ( ClientExtendedValidationAndFlowControl clientExtendedValidationAndFlowControl ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithOfflineQueueBehavior,
                 ( ClientOperationQueueBehaviorType offlineQueueBehavior ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithReconnectOptions,
                 ( ReconnectOptions reconnectOptions ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &, WithPingTimeoutMs, ( uint32_t pingTimeoutMs ), ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithConnackTimeoutMs,
                 ( uint32_t connackTimeoutMs ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &,
                 WithAckTimeoutSeconds,
                 ( uint32_t ackTimeoutSeconds ),
                 ( noexcept, override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &, WithSdkName, ( const Crt::String &sdkName ), ( override ) );

    MOCK_METHOD( MqttClientBuilderWrapper &, WithSdkVersion, ( const Crt::String &sdkVersion ), ( override ) );

    MqttClientBuilderWrapper &
    WithClientConnectionSuccessCallback( OnConnectionSuccessHandler callback ) noexcept override
    {
        mOnConnectionSuccessCallback = callback;
        return *this;
    }

    MqttClientBuilderWrapper &
    WithClientConnectionFailureCallback( OnConnectionFailureHandler callback ) noexcept override
    {
        mOnConnectionFailureCallback = callback;
        return *this;
    }

    MqttClientBuilderWrapper &
    WithClientDisconnectionCallback( OnDisconnectionHandler callback ) noexcept override
    {
        mOnDisconnectionCallback = callback;
        return *this;
    }

    MqttClientBuilderWrapper &
    WithClientStoppedCallback( OnStoppedHandler callback ) noexcept override
    {
        mOnStoppedCallback = callback;
        return *this;
    }

    MqttClientBuilderWrapper &
    WithClientAttemptingConnectCallback( OnAttemptingConnectHandler callback ) noexcept override
    {
        mOnAttemptingConnectCallback = callback;
        return *this;
    }

    MqttClientBuilderWrapper &
    WithPublishReceivedCallback( OnPublishReceivedHandler callback ) noexcept override
    {
        mOnPublishReceivedHandlerCallback = callback;
        return *this;
    }

    Aws::Crt::Mqtt5::OnConnectionSuccessHandler mOnConnectionSuccessCallback;
    Aws::Crt::Mqtt5::OnConnectionFailureHandler mOnConnectionFailureCallback;
    Aws::Crt::Mqtt5::OnDisconnectionHandler mOnDisconnectionCallback;
    Aws::Crt::Mqtt5::OnStoppedHandler mOnStoppedCallback;
    Aws::Crt::Mqtt5::OnAttemptingConnectHandler mOnAttemptingConnectCallback;
    Aws::Crt::Mqtt5::OnPublishReceivedHandler mOnPublishReceivedHandlerCallback;

private:
    std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> mMqttClientBuilder;
};

} // namespace IoTFleetWise
} // namespace Aws
