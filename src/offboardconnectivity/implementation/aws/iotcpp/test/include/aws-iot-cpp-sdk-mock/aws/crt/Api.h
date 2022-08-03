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

#pragma once
#include "AwsIotSdkMock.h"
#include "Types.h"

using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot::Testing;

inline const char *
aws_error_debug_str( int err )
{
    return getSdkMock()->aws_error_debug_str( err );
}

inline void
aws_byte_buf_clean_up( struct aws_byte_buf *buf )
{
    getSdkMock()->aws_byte_buf_clean_up( buf );
}

namespace Aws
{
namespace Crt
{

inline Allocator *
DefaultAllocator() noexcept
{
    return getSdkMock()->DefaultAllocator();
}
inline ByteBuf
ByteBufNewCopy( Allocator *alloc, const uint8_t *array, size_t len )
{
    return getSdkMock()->ByteBufNewCopy( alloc, array, len );
}

inline ByteCursor
ByteCursorFromCString( const char *str )
{
    return getSdkMock()->ByteCursorFromCString( str );
}
class ApiHandle
{
public:
    ApiHandle(){};
    ApiHandle( Allocator *allocator )
    {
        mAllocator = allocator;
        static_cast<void>( allocator );
    };
    static Allocator *
    getLatestAllocator()
    {
        return mAllocator;
    }

private:
    static Allocator *mAllocator;
};

inline const char *
ErrorDebugString( int error ) noexcept
{
    return getSdkMock()->ErrorDebugString( error );
}

namespace Io
{
class EventLoopGroup final
{
public:
    EventLoopGroup( uint16_t threadCount = 0 ) noexcept
    {
        getEventLoopMock()->CONSTRUCTOR( threadCount );
    }
    ~EventLoopGroup(){};

    operator bool() const
    {
        return getEventLoopMock()->operatorBool();
    }
    int LastError() const;

private:
    int m_lastError{};
};
class HostResolver
{
};
class DefaultHostResolver final : public HostResolver
{
public:
    DefaultHostResolver( EventLoopGroup &, size_t, size_t, Allocator *a = nullptr ) noexcept
    {
        (void)a;
    };
    ~DefaultHostResolver(){};
    DefaultHostResolver( const DefaultHostResolver & ) = delete;
    DefaultHostResolver &operator=( const DefaultHostResolver & ) = delete;
    DefaultHostResolver( DefaultHostResolver && ) = delete;
    DefaultHostResolver &operator=( DefaultHostResolver && ) = delete;
    operator bool() const noexcept
    {
        return false;
    }

private:
};
class ClientBootstrap final
{
public:
    ClientBootstrap() = default;
    ClientBootstrap( EventLoopGroup &elGroup, HostResolver &resolver, Allocator *allocator = NULL ) noexcept
    {
        getClientBootstrapMock()->CONSTRUCTOR( elGroup, resolver, allocator );
    }
    ~ClientBootstrap(){};
    ClientBootstrap( const ClientBootstrap & ) = delete;
    ClientBootstrap &operator=( const ClientBootstrap & ) = delete;
    ClientBootstrap( ClientBootstrap && ) = delete;
    ClientBootstrap &operator=( ClientBootstrap && ) = delete;

    operator bool() const noexcept
    {
        return getClientBootstrapMock()->operatorBool();
    }

    int
    LastError() const noexcept
    {
        return getClientBootstrapMock()->LastError();
    }
};
} // namespace Io
} // namespace Crt
} // namespace Aws
