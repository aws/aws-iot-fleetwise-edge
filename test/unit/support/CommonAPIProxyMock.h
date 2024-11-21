// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CommonAPI/CommonAPI.hpp>
#if !defined( COMMONAPI_INTERNAL_COMPILATION )
#define COMMONAPI_INTERNAL_COMPILATION
#define HAS_DEFINED_COMMONAPI_INTERNAL_COMPILATION_HERE
#endif
#include <CommonAPI/Proxy.hpp>
#ifdef HAS_DEFINED_COMMONAPI_INTERNAL_COMPILATION_HERE
#undef COMMONAPI_INTERNAL_COMPILATION
#undef HAS_DEFINED_COMMONAPI_INTERNAL_COMPILATION_HERE
#endif
#include <gmock/gmock.h>

class CommonAPIProxyMock : public CommonAPI::Proxy
{
public:
    MOCK_METHOD( const CommonAPI::Address &, getAddress, (), ( const ) );
    MOCK_METHOD( std::future<void>, getCompletionFuture, () );
    MOCK_METHOD( bool, isAvailable, (), ( const ) );
    MOCK_METHOD( bool, isAvailableBlocking, (), ( const ) );
    MOCK_METHOD( CommonAPI::ProxyStatusEvent &, getProxyStatusEvent, () );
    MOCK_METHOD( CommonAPI::InterfaceVersionAttribute &, getInterfaceVersionAttribute, () );
};
