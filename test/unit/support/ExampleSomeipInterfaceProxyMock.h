// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "v1/commonapi/ExampleSomeipInterfaceProxy.hpp"
#include <gmock/gmock.h>

template <typename... _AttributeExtensions>
class ExampleSomeipInterfaceProxyMock : public v1::commonapi::ExampleSomeipInterfaceProxy<>
{
public:
    ExampleSomeipInterfaceProxyMock( std::shared_ptr<CommonAPI::Proxy> proxy )
        : ExampleSomeipInterfaceProxy( proxy ){};
    MOCK_METHOD( const CommonAPI::Address &, getAddress, (), ( const ) );
    MOCK_METHOD( bool, isAvailable, (), ( const ) );
    MOCK_METHOD( bool, isAvailableBlocking, (), ( const ) );
    MOCK_METHOD( CommonAPI::ProxyStatusEvent &, getProxyStatusEvent, () );
    MOCK_METHOD( CommonAPI::InterfaceVersionAttribute &, getInterfaceVersionAttribute, () );
    MOCK_METHOD( std::future<void>, getCompletionFuture, () );
    MOCK_METHOD( v1::commonapi::ExampleSomeipInterfaceProxyBase::XAttribute &, getXAttribute, () );
    MOCK_METHOD( v1::commonapi::ExampleSomeipInterfaceProxyBase::A1Attribute &, getA1Attribute, () );
    MOCK_METHOD( void,
                 setInt32,
                 ( int32_t _value, CommonAPI::CallStatus &_internalCallStatus, const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setInt32Async,
                 ( const int32_t &_value,
                   v1::commonapi::ExampleSomeipInterfaceProxyBase::SetInt32AsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setInt64Async,
                 ( const int64_t &_value,
                   v1::commonapi::ExampleSomeipInterfaceProxyBase::SetInt64AsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setBooleanAsync,
                 ( const bool &_value,
                   v1::commonapi::ExampleSomeipInterfaceProxyBase::SetBooleanAsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setFloatAsync,
                 ( const float &_value,
                   v1::commonapi::ExampleSomeipInterfaceProxyBase::SetFloatAsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setDoubleAsync,
                 ( const double &_value,
                   v1::commonapi::ExampleSomeipInterfaceProxyBase::SetDoubleAsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setStringAsync,
                 ( const std::string &_value,
                   v1::commonapi::ExampleSomeipInterfaceProxyBase::SetStringAsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( void,
                 getInt32,
                 ( CommonAPI::CallStatus & _internalCallStatus, uint32_t &_value, const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 getInt32Async,
                 ( v1::commonapi::ExampleSomeipInterfaceProxyBase::GetInt32AsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( v1::commonapi::ExampleSomeipInterfaceProxyBase::NotifyLRCStatusEvent &, getNotifyLRCStatusEvent, () );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setInt32LongRunningAsync,
                 ( const std::string &_commandId,
                   const int32_t &_value,
                   SetInt32LongRunningAsyncCallback _callback,
                   const CommonAPI::CallInfo *_info ) );
};

template <typename T>
class CommonAPIObservableAttributeMock : public CommonAPI::ObservableAttribute<T>
{
public:
    typedef typename CommonAPI::ObservableAttribute<T>::ChangedEvent ChangedEvent;
    MOCK_METHOD( ChangedEvent &, getChangedEvent, () );
    MOCK_METHOD( void,
                 getValue,
                 ( CommonAPI::CallStatus & _internalCallStatus, T &_value, const CommonAPI::CallInfo *_info ),
                 ( const ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 getValueAsync,
                 ( std::function<void( const CommonAPI::CallStatus &, T )> attributeAsyncCallback,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( void,
                 setValue,
                 ( const T &requestValue,
                   CommonAPI::CallStatus &_internalCallStatus,
                   T &responseValue,
                   const CommonAPI::CallInfo *_info ) );
    MOCK_METHOD( std::future<CommonAPI::CallStatus>,
                 setValueAsync,
                 ( const T &requestValue,
                   std::function<void( const CommonAPI::CallStatus &, T )> attributeAsyncCallback,
                   const CommonAPI::CallInfo *_info ) );
};

template <typename T>
class CommonAPIObservableAttributeChangedEventMock : public CommonAPI::ObservableAttribute<T>::ChangedEvent
{
public:
    MOCK_METHOD( void, onFirstListenerAdded, ( const std::function<void( const T & )> &_listener ) );
};

template <typename... T>
class CommonAPIEventMock : public CommonAPI::Event<T...>
{
public:
    MOCK_METHOD( void, onFirstListenerAdded, ( const std::function<void( const T &... )> &_listener ) );
};
