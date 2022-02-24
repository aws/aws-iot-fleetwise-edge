/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

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

struct aws_byte_buf
{
    size_t len;
    uint8_t *buffer;
    size_t capacity;
    struct aws_allocator *allocator;
};

enum aws_mqtt_qos
{
    AWS_MQTT_QOS_AT_MOST_ONCE = 0x0,
    AWS_MQTT_QOS_AT_LEAST_ONCE = 0x1,
    AWS_MQTT_QOS_EXACTLY_ONCE = 0x2,
    AWS_MQTT_QOS_FAILURE = 0x80,
};
enum aws_mqtt_connect_return_code
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
