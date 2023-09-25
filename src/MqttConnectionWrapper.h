// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/crt/mqtt/MqttClient.h>
#include <cstdint>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief   A wrapper around MqttConnection so that we can provide different implementations.
 *
 * The original MqttConnection can't be inherited from because it only declares private constructors.
 **/
class MqttConnectionWrapper
{
public:
    /**
     * @param mqttConnection the MqttConnection instance to be wrapped
     */
    MqttConnectionWrapper( std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection )
        : mMqttConnection( std::move( mqttConnection ) ){};
    virtual ~MqttConnectionWrapper() = default;

    MqttConnectionWrapper() = delete;
    MqttConnectionWrapper( const MqttConnectionWrapper & ) = delete;
    MqttConnectionWrapper &operator=( const MqttConnectionWrapper & ) = delete;
    MqttConnectionWrapper( MqttConnectionWrapper && ) = delete;
    MqttConnectionWrapper &operator=( MqttConnectionWrapper && ) = delete;

    using OnConnectionInterruptedHandler = std::function<void( MqttConnectionWrapper &connection, int error )>;
    using OnConnectionResumedHandler = std::function<void(
        MqttConnectionWrapper &connection, Aws::Crt::Mqtt::ReturnCode connectCode, bool sessionPresent )>;
    using OnConnectionCompletedHandler = std::function<void(
        MqttConnectionWrapper &connection, int errorCode, Aws::Crt::Mqtt::ReturnCode returnCode, bool sessionPresent )>;
    using OnSubAckHandler = std::function<void( MqttConnectionWrapper &connection,
                                                uint16_t packetId,
                                                const Aws::Crt::String &topic,
                                                Aws::Crt::Mqtt::QOS qos,
                                                int errorCode )>;
    using OnDisconnectHandler = std::function<void( MqttConnectionWrapper &connection )>;
    using OnMessageReceivedHandler = std::function<void( MqttConnectionWrapper &connection,
                                                         const Aws::Crt::String &topic,
                                                         const Aws::Crt::ByteBuf &payload,
                                                         bool dup,
                                                         Aws::Crt::Mqtt::QOS qos,
                                                         bool retain )>;
    using OnOperationCompleteHandler =
        std::function<void( MqttConnectionWrapper &connection, uint16_t packetId, int errorCode )>;

    virtual explicit operator bool() const noexcept
    {
        return *mMqttConnection ? true : false;
    }
    virtual int
    LastError() const noexcept
    {
        return mMqttConnection->LastError();
    }
    virtual bool
    Connect( const char *clientId,
             bool cleanSession,
             uint16_t keepAliveTimeSecs = 0,
             uint32_t pingTimeoutMs = 0 ) noexcept
    {
        return mMqttConnection->Connect( clientId, cleanSession, keepAliveTimeSecs, pingTimeoutMs );
    }
    virtual bool
    Disconnect() noexcept
    {
        return mMqttConnection->Disconnect();
    }
    virtual uint16_t
    Subscribe( const char *topicFilter,
               Aws::Crt::Mqtt::QOS qos,
               OnMessageReceivedHandler &&onMessage,
               OnSubAckHandler &&onSubAck ) noexcept
    {
        return mMqttConnection->Subscribe(
            topicFilter,
            qos,
            [this, onMessageCallback = std::move( onMessage )]( Aws::Crt::Mqtt::MqttConnection &connection,
                                                                const Aws::Crt::String &topic,
                                                                const Aws::Crt::ByteBuf &payload,
                                                                bool dup,
                                                                Aws::Crt::Mqtt::QOS qosCallback,
                                                                bool retain ) {
                static_cast<void>( connection );
                onMessageCallback( *this, topic, payload, dup, qosCallback, retain );
            },
            [this, onSubAckCallback = std::move( onSubAck )]( Aws::Crt::Mqtt::MqttConnection &connection,
                                                              uint16_t packetId,
                                                              const Aws::Crt::String &topic,
                                                              Aws::Crt::Mqtt::QOS qosCallback,
                                                              int errorCode ) {
                static_cast<void>( connection );
                onSubAckCallback( *this, packetId, topic, qosCallback, errorCode );
            } );
    }
    virtual bool
    SetOnMessageHandler( OnMessageReceivedHandler &&onMessage ) noexcept
    {
        return mMqttConnection->SetOnMessageHandler(
            [this, onMessageCallback = std::move( onMessage )]( Aws::Crt::Mqtt::MqttConnection &connection,
                                                                const Aws::Crt::String &topic,
                                                                const Aws::Crt::ByteBuf &payload,
                                                                bool dup,
                                                                Aws::Crt::Mqtt::QOS qos,
                                                                bool retain ) {
                static_cast<void>( connection );
                onMessageCallback( *this, topic, payload, dup, qos, retain );
            } );
    }
    virtual uint16_t
    Unsubscribe( const char *topicFilter, OnOperationCompleteHandler &&onOpComplete ) noexcept
    {
        return mMqttConnection->Unsubscribe(
            topicFilter,
            [this, onOpCompleteCallback = std::move( onOpComplete )](
                Aws::Crt::Mqtt::MqttConnection &connection, uint16_t packetId, int errorCode ) {
                static_cast<void>( connection );
                onOpCompleteCallback( *this, packetId, errorCode );
            } );
    }
    virtual uint16_t
    Publish( const char *topic,
             Aws::Crt::Mqtt::QOS qos,
             bool retain,
             const Aws::Crt::ByteBuf &payload,
             OnOperationCompleteHandler &&onOpComplete ) noexcept
    {
        return mMqttConnection->Publish(
            topic,
            qos,
            retain,
            payload,
            [this, onOpCompleteCallback = std::move( onOpComplete )](
                Aws::Crt::Mqtt::MqttConnection &connection, uint16_t packetId, int errorCode ) {
                static_cast<void>( connection );
                onOpCompleteCallback( *this, packetId, errorCode );
            } );
    }

    virtual void
    SetOnConnectionInterrupted( OnConnectionInterruptedHandler onConnectionInterrupted )
    {
        mMqttConnection->OnConnectionInterrupted =
            [this, onConnectionInterrupted]( Aws::Crt::Mqtt::MqttConnection &connection, int error ) {
                static_cast<void>( connection );
                onConnectionInterrupted( *this, error );
            };
    }
    virtual void
    SetOnConnectionResumed( OnConnectionResumedHandler onConnectionResumed )
    {
        mMqttConnection->OnConnectionResumed = [this, onConnectionResumed]( Aws::Crt::Mqtt::MqttConnection &connection,
                                                                            Aws::Crt::Mqtt::ReturnCode connectCode,
                                                                            bool sessionPresent ) {
            static_cast<void>( connection );
            onConnectionResumed( *this, connectCode, sessionPresent );
        };
    }
    virtual void
    SetOnConnectionCompleted( OnConnectionCompletedHandler onConnectionCompleted )
    {
        mMqttConnection->OnConnectionCompleted = [this,
                                                  onConnectionCompleted]( Aws::Crt::Mqtt::MqttConnection &connection,
                                                                          int errorCode,
                                                                          Aws::Crt::Mqtt::ReturnCode returnCode,
                                                                          bool sessionPresent ) {
            static_cast<void>( connection );
            onConnectionCompleted( *this, errorCode, returnCode, sessionPresent );
        };
    }
    virtual void
    SetOnDisconnect( OnDisconnectHandler onDisconnect )
    {
        mMqttConnection->OnDisconnect = [this, onDisconnect]( Aws::Crt::Mqtt::MqttConnection &connection ) {
            static_cast<void>( connection );
            onDisconnect( *this );
        };
    }

private:
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mMqttConnection;
};

} // namespace IoTFleetWise
} // namespace Aws
