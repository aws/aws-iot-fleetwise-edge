// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "aws/crt/Types.h"
#include <functional>
#include <memory>
#include <string>
namespace Aws
{

namespace Crt
{

namespace Mqtt
{
using QOS = aws_mqtt_qos;
using String = std::string;
using ReturnCode = aws_mqtt_connect_return_code;
class MqttConnection;

using OnConnectionInterruptedHandler = std::function<void( MqttConnection &connection, int error )>;

using OnConnectionResumedHandler =
    std::function<void( MqttConnection &connection, ReturnCode connectCode, bool sessionPresent )>;

using OnConnectionCompletedHandler =
    std::function<void( MqttConnection &connection, int errorCode, ReturnCode returnCode, bool sessionPresent )>;

using OnSubAckHandler =
    std::function<void( MqttConnection &connection, uint16_t packetId, const String &topic, QOS qos, int errorCode )>;

using OnDisconnectHandler = std::function<void( MqttConnection &connection )>;

class MqttConnection
{
public:
    using OnMessageReceivedHandler = std::function<void(
        MqttConnection &connection, const String &topic, const ByteBuf &payload, bool dup, QOS qos, bool retain )>;

    using OnSubAckHandler = std::function<void(
        MqttConnection &connection, uint16_t packetId, const String &topic, QOS qos, int errorCode )>;

    using OnOperationCompleteHandler =
        std::function<void( MqttConnection &connection, uint16_t packetId, int errorCode )>;

    virtual uint16_t Unsubscribe( const char *topicFilter, OnOperationCompleteHandler &&onOpComplete ) noexcept = 0;

    virtual uint16_t Subscribe( const char *topicFilter,
                                QOS qos,
                                OnMessageReceivedHandler &&onMessage,
                                OnSubAckHandler &&onSubAck ) noexcept = 0;

    virtual uint16_t Publish( const char *topic,
                              QOS qos,
                              bool retain,
                              const ByteBuf &payload,
                              OnOperationCompleteHandler &&onOpComplete ) noexcept = 0;

    virtual bool Connect( const char *clientId,
                          bool cleanSession,
                          uint16_t keepAliveTimeSecs = 0,
                          uint32_t pingTimeoutMs = 0 ) noexcept = 0;

    virtual bool Disconnect() noexcept = 0;

    virtual bool SetOnMessageHandler( OnMessageReceivedHandler &&onMessage ) noexcept = 0;

    OnConnectionInterruptedHandler OnConnectionInterrupted;
    OnConnectionResumedHandler OnConnectionResumed;
    OnConnectionCompletedHandler OnConnectionCompleted;
    OnDisconnectHandler OnDisconnect;

    virtual int LastError() const noexcept = 0;
};

} // namespace Mqtt
} // namespace Crt

namespace Iot
{
class MqttClientConnectionConfig
{
public:
    virtual explicit operator bool() const noexcept;
    virtual int LastError() const noexcept;
};
class MqttClientConnectionConfigBuilder final
{
public:
    MqttClientConnectionConfigBuilder()
    {
    }
    MqttClientConnectionConfigBuilder( const char *, const char *, Crt::Allocator *a = nullptr ) noexcept
    {
        (void)a;
    }
    MqttClientConnectionConfigBuilder( const Crt::ByteCursor &,
                                       const Crt::ByteCursor &,
                                       Crt::Allocator *a = nullptr ) noexcept
    {
        (void)a;
    }

    MqttClientConnectionConfigBuilder &WithEndpoint( const Crt::String &endpoint );
    MqttClientConnectionConfigBuilder &WithCertificateAuthority( const Crt::ByteCursor &rootCA );
    MqttClientConnectionConfig Build() noexcept;
};
class MqttClient
{
public:
    MqttClient( Aws::Crt::Io::ClientBootstrap &bootstrap, Crt::Allocator *allocator = NULL ) noexcept;

    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> NewConnection( const MqttClientConnectionConfig &config ) noexcept;
    int LastError() const noexcept;
    explicit operator bool() const noexcept;
};
} // namespace Iot
} // namespace Aws
