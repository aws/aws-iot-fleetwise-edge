#pragma once
#include "aws/crt/Types.h"
#include "aws/iot/MqttClient.h"
#include <gmock/gmock.h> // Brings in gMock.
namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{
namespace Testing
{

class AwsIotSdkMock
{
public:
    MOCK_METHOD( (const char *), aws_error_debug_str, (int));
    MOCK_METHOD( (void), aws_byte_buf_clean_up, ( struct aws_byte_buf * buf ) );
    MOCK_METHOD( (const char *), ErrorDebugString, (int));
    MOCK_METHOD( (struct aws_allocator *), DefaultAllocator, () );
    MOCK_METHOD( ( struct aws_byte_buf ),
                 ByteBufNewCopy,
                 ( struct aws_allocator * alloc, const uint8_t *array, size_t len ) );
    MOCK_METHOD( ( struct aws_byte_cursor ), ByteCursorFromCString, ( const char *str ) );
    MOCK_METHOD( ( Aws::Crt::String ), UUIDToString, () );
};

using Aws::Crt::Mqtt::MqttConnection;
class MqttConnectionMock : public Aws::Crt::Mqtt::MqttConnection
{
public:
    MOCK_METHOD( ( uint16_t ),
                 Publish,
                 ( const char *topic,
                   aws_mqtt_qos qos,
                   bool retain,
                   const struct aws_byte_buf &payload,
                   MqttConnection::OnOperationCompleteHandler &&onOpComplete ),
                 ( noexcept ) );
    MOCK_METHOD( ( uint16_t ),
                 Unsubscribe,
                 ( const char *topicFilter, MqttConnection::OnOperationCompleteHandler &&onOpComplete ),
                 ( noexcept, override ) );
    MOCK_METHOD( ( uint16_t ),
                 Subscribe,
                 ( const char *topicFilter,
                   aws_mqtt_qos qos,
                   MqttConnection::OnMessageReceivedHandler &&onMessage,
                   MqttConnection::OnSubAckHandler &&onSubAck ),
                 ( noexcept ) );
    MOCK_METHOD( (bool), Disconnect, (), ( noexcept ) );
    MOCK_METHOD( (bool), SetOnMessageHandler, ( MqttConnection::OnMessageReceivedHandler && onMessage ), ( noexcept ) );
    MOCK_METHOD( (int), LastError, (), ( const, noexcept ) );
    MOCK_METHOD( (bool),
                 Connect,
                 ( const char *clientId, bool cleanSession, uint16_t keepAliveTimeSecs, uint32_t pingTimeoutMs ),
                 ( noexcept ) );
};

class MqttClientMock
{
public:
    MOCK_METHOD( (void), CONSTRUCTOR, ( Aws::Crt::Io::ClientBootstrap & bootstrap, Aws::Crt::Allocator *allocator ) );
    MOCK_METHOD( (std::shared_ptr<Aws::Crt::Mqtt::MqttConnection>),
                 NewConnection,
                 ( const Aws::Iot::MqttClientConnectionConfig &config ) );
    MOCK_METHOD( (bool), operatorBool, () );
    MOCK_METHOD( (int), LastError, () );
};

class MqttClientConnectionConfigMock
{
public:
    MOCK_METHOD( (bool), operatorBool, () );
    MOCK_METHOD( (int), LastError, () );
};

class MqttClientConnectionConfigBuilderMock
{
public:
    MOCK_METHOD( (void), WithEndpoint, ( const Aws::Crt::String &endpoint ) );
    MOCK_METHOD( (void), WithCertificateAuthority, ( const Crt::ByteCursor &rootCA ) );
    MOCK_METHOD( ( Aws::Iot::MqttClientConnectionConfig ), Build, () );
};

class ClientBootstrapMock
{
public:
    MOCK_METHOD( (void),
                 CONSTRUCTOR,
                 ( Aws::Crt::Io::EventLoopGroup & elGroup,
                   Aws::Crt::Io::HostResolver &resolver,
                   Aws::Crt::Allocator *allocator ) );
    MOCK_METHOD( (bool), operatorBool, () );
    MOCK_METHOD( (int), LastError, () );
};

class EventLoopGroupMock
{
public:
    MOCK_METHOD( (void), CONSTRUCTOR, ( uint16_t threadCount ) );
    MOCK_METHOD( (bool), operatorBool, () );
};

// These objects are instantiated by the software under test (SUT) directly or copied (not by reference)
// So with link time injection only one mock exists and the linked test files in this test folder
// forwards to the single instance mocks.
AwsIotSdkMock *getSdkMock();
void setSdkMock( AwsIotSdkMock *m );

MqttConnectionMock *getConMock();
void setConMock( MqttConnectionMock *m );

MqttClientMock *getClientMock();
void setClientMock( MqttClientMock *m );

MqttClientConnectionConfigMock *getConfMock();
void setConfMock( MqttClientConnectionConfigMock *m );

MqttClientConnectionConfigBuilderMock *getConfBuilderMock();
void setConfBuilderMock( MqttClientConnectionConfigBuilderMock *m );

ClientBootstrapMock *getClientBootstrapMock();
void setClientBootstrapMock( ClientBootstrapMock *m );

EventLoopGroupMock *getEventLoopMock();
void setEventLoopMock( EventLoopGroupMock *m );

} // namespace Testing
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws
