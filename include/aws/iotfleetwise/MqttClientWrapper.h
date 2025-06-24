// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/iot/Mqtt5Client.h>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief   A wrapper around Mqtt5Client so that we can provide different implementations.
 *
 * The original Mqtt5Client can't be inherited from because it only declares private constructors.
 **/
class MqttClientWrapper
{
public:
    /**
     * @param mqttClient the Mqtt5Client instance to be wrapped
     */
    MqttClientWrapper( std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> mqttClient )
        : mMqttClient( std::move( mqttClient ) ){};
    virtual ~MqttClientWrapper() = default;

    MqttClientWrapper() = delete;
    MqttClientWrapper( const MqttClientWrapper & ) = delete;
    MqttClientWrapper &operator=( const MqttClientWrapper & ) = delete;
    MqttClientWrapper( MqttClientWrapper && ) = delete;
    MqttClientWrapper &operator=( MqttClientWrapper && ) = delete;

    virtual explicit operator bool() const noexcept
    {
        return *mMqttClient ? true : false;
    }

    virtual int
    LastError() const noexcept
    {
        return mMqttClient->LastError();
    }

    virtual bool
    Start() const noexcept
    {
        return mMqttClient->Start();
    }

    virtual bool
    Stop() noexcept
    {
        return mMqttClient->Stop();
    }

    virtual bool
    Stop( std::shared_ptr<Aws::Crt::Mqtt5::DisconnectPacket> disconnectOptions ) noexcept
    {
        return mMqttClient->Stop( std::move( disconnectOptions ) );
    }

    virtual bool
    Publish( std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket> publishOptions,
             Aws::Crt::Mqtt5::OnPublishCompletionHandler onPublishCompletionCallback = nullptr ) noexcept
    {
        return mMqttClient->Publish( std::move( publishOptions ), std::move( onPublishCompletionCallback ) );
    }

    virtual bool
    Subscribe( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket> subscribeOptions,
               Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback = nullptr ) noexcept
    {
        return mMqttClient->Subscribe( std::move( subscribeOptions ), std::move( onSubscribeCompletionCallback ) );
    }

    virtual bool
    Unsubscribe( std::shared_ptr<UnsubscribePacket> unsubscribeOptions,
                 Aws::Crt::Mqtt5::OnUnsubscribeCompletionHandler onUnsubscribeCompletionCallback = nullptr ) noexcept
    {
        return mMqttClient->Unsubscribe( std::move( unsubscribeOptions ),
                                         std::move( onUnsubscribeCompletionCallback ) );
    }

    virtual const Aws::Crt::Mqtt5::Mqtt5ClientOperationStatistics &
    GetOperationStatistics() noexcept
    {
        return mMqttClient->GetOperationStatistics();
    }

private:
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> mMqttClient;
};

/**
 * @brief A wrapper around Mqtt5ClientBuilder so that we can provide different implementations.
 *
 * The original MqttClient can't be inherited from.
 **/
class MqttClientBuilderWrapper
{
public:
    /**
     * @param mqttClientBuilder the Mqtt5ClientBuilder instance to be wrapped
     */
    MqttClientBuilderWrapper( std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> mqttClientBuilder )
        : mMqttClientBuilder( std::move( mqttClientBuilder ) ){};
    virtual ~MqttClientBuilderWrapper() = default;

    MqttClientBuilderWrapper() = delete;
    MqttClientBuilderWrapper( const MqttClientBuilderWrapper & ) = delete;
    MqttClientBuilderWrapper &operator=( const MqttClientBuilderWrapper & ) = delete;
    MqttClientBuilderWrapper( MqttClientBuilderWrapper && ) = delete;
    MqttClientBuilderWrapper &operator=( MqttClientBuilderWrapper && ) = delete;

    virtual int
    LastError() const noexcept
    {
        return mMqttClientBuilder->LastError();
    }

    virtual explicit operator bool() const noexcept
    {
        return *mMqttClientBuilder ? true : false;
    }

    virtual std::shared_ptr<MqttClientWrapper>
    Build() noexcept
    {
        auto mMqttClient = mMqttClientBuilder->Build();
        if ( mMqttClient == nullptr )
        {
            return nullptr;
        }

        return std::make_shared<MqttClientWrapper>( mMqttClient );
    }

    virtual MqttClientBuilderWrapper &
    WithHostName( Crt::String hostname )
    {
        mMqttClientBuilder->WithHostName( std::move( hostname ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithPort( uint16_t port ) noexcept
    {
        mMqttClientBuilder->WithPort( port );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithCertificateAuthority( const Crt::ByteCursor &cert ) noexcept
    {
        mMqttClientBuilder->WithCertificateAuthority( cert );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithHttpProxyOptions( const Crt::Http::HttpClientConnectionProxyOptions &proxyOptions ) noexcept
    {
        mMqttClientBuilder->WithHttpProxyOptions( proxyOptions );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithCustomAuthorizer( const Iot::Mqtt5CustomAuthConfig &config ) noexcept
    {
        mMqttClientBuilder->WithCustomAuthorizer( config );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithConnectOptions( std::shared_ptr<ConnectPacket> packetConnect ) noexcept
    {
        mMqttClientBuilder->WithConnectOptions( std::move( packetConnect ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithSessionBehavior( ClientSessionBehaviorType sessionBehavior ) noexcept
    {
        mMqttClientBuilder->WithSessionBehavior( sessionBehavior );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithClientExtendedValidationAndFlowControl(
        ClientExtendedValidationAndFlowControl clientExtendedValidationAndFlowControl ) noexcept
    {
        mMqttClientBuilder->WithClientExtendedValidationAndFlowControl( clientExtendedValidationAndFlowControl );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithOfflineQueueBehavior( ClientOperationQueueBehaviorType offlineQueueBehavior ) noexcept
    {
        mMqttClientBuilder->WithOfflineQueueBehavior( offlineQueueBehavior );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithReconnectOptions( ReconnectOptions reconnectOptions ) noexcept
    {
        mMqttClientBuilder->WithReconnectOptions( reconnectOptions );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithPingTimeoutMs( uint32_t pingTimeoutMs ) noexcept
    {
        mMqttClientBuilder->WithPingTimeoutMs( pingTimeoutMs );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithConnackTimeoutMs( uint32_t connackTimeoutMs ) noexcept
    {
        mMqttClientBuilder->WithConnackTimeoutMs( connackTimeoutMs );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithAckTimeoutSeconds( uint32_t ackTimeoutSeconds ) noexcept
    {
        mMqttClientBuilder->WithAckTimeoutSeconds( ackTimeoutSeconds );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithSdkName( const Crt::String &sdkName )
    {
        mMqttClientBuilder->WithSdkName( sdkName );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithSdkVersion( const Crt::String &sdkVersion )
    {
        mMqttClientBuilder->WithSdkVersion( sdkVersion );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithClientConnectionSuccessCallback( OnConnectionSuccessHandler callback ) noexcept
    {
        mMqttClientBuilder->WithClientConnectionSuccessCallback( std::move( callback ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithClientConnectionFailureCallback( OnConnectionFailureHandler callback ) noexcept
    {
        mMqttClientBuilder->WithClientConnectionFailureCallback( std::move( callback ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithClientDisconnectionCallback( OnDisconnectionHandler callback ) noexcept
    {
        mMqttClientBuilder->WithClientDisconnectionCallback( std::move( callback ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithClientStoppedCallback( OnStoppedHandler callback ) noexcept
    {
        mMqttClientBuilder->WithClientStoppedCallback( std::move( callback ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithClientAttemptingConnectCallback( OnAttemptingConnectHandler callback ) noexcept
    {
        mMqttClientBuilder->WithClientAttemptingConnectCallback( std::move( callback ) );
        return *this;
    }

    virtual MqttClientBuilderWrapper &
    WithPublishReceivedCallback( OnPublishReceivedHandler callback ) noexcept
    {
        mMqttClientBuilder->WithPublishReceivedCallback( std::move( callback ) );
        return *this;
    }

private:
    std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> mMqttClientBuilder;
};

} // namespace IoTFleetWise
} // namespace Aws
