// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string>
#pragma once
struct aws_allocator
{
    void *( *mem_acquire )( struct aws_allocator *allocator, size_t size );
    void ( *mem_release )( struct aws_allocator *allocator, void *ptr );
    void *( *mem_realloc )( struct aws_allocator *allocator, void *oldptr, size_t oldsize, size_t newsize );
    void *( *mem_calloc )( struct aws_allocator *allocator, size_t num, size_t size );
    void *impl;
};

struct aws_byte_cursor
{
    /* do not reorder this, this struct lines up nicely with windows buffer structures--saving us allocations */
    size_t len;
    uint8_t *ptr;
};

struct aws_byte_buf
{
    size_t len;
    uint8_t *buffer;
    size_t capacity;
    struct aws_allocator *allocator;
};

enum class aws_mqtt_qos
{
    AWS_MQTT_QOS_AT_MOST_ONCE = 0x0,
    AWS_MQTT_QOS_AT_LEAST_ONCE = 0x1,
    AWS_MQTT_QOS_EXACTLY_ONCE = 0x2,
    AWS_MQTT_QOS_FAILURE = 0x80,
};
enum class aws_mqtt_connect_return_code
{
    AWS_MQTT_CONNECT_ACCEPTED,
    AWS_MQTT_CONNECT_UNACCEPTABLE_PROTOCOL_VERSION,
    AWS_MQTT_CONNECT_IDENTIFIER_REJECTED,
    AWS_MQTT_CONNECT_SERVER_UNAVAILABLE,
    AWS_MQTT_CONNECT_BAD_USERNAME_OR_PASSWORD,
    AWS_MQTT_CONNECT_NOT_AUTHORIZED,
};

namespace Aws
{
namespace Crt
{

using Allocator = struct aws_allocator;
using ByteBuf = struct aws_byte_buf;
using ByteCursor = struct aws_byte_cursor;
using String = std::string;
namespace Io
{

class ClientBootstrap;
class EventLoopGroup;
class HostResolver;
} // namespace Io
} // namespace Crt
namespace Iot
{
class MqttClientConnectionConfig;
}
} // namespace Aws
