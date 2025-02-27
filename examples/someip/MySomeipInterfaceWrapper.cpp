// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MySomeipInterfaceWrapper.h"
#include <aws/iotfleetwise/LoggingModule.h>
#include <utility>

MySomeipInterfaceWrapper::MySomeipInterfaceWrapper(
    std::string domain,
    std::string instance,
    std::string connection,
    std::function<std::shared_ptr<v1::commonapi::MySomeipInterfaceProxy<>>( std::string, std::string, std::string )>
        buildProxy )
    : mDomain( std::move( domain ) )
    , mInstance( std::move( instance ) )
    , mConnection( std::move( connection ) )
    , mBuildProxy( std::move( buildProxy ) )
{
}

bool
MySomeipInterfaceWrapper::init()
{
    mProxy = mBuildProxy( mDomain, mInstance, mConnection );
    if ( mProxy == nullptr )
    {
        FWE_LOG_ERROR( "Failed to build proxy " );
        return false;
    }
    return true;
}

std::shared_ptr<CommonAPI::Proxy>
MySomeipInterfaceWrapper::getProxy() const
{
    return std::static_pointer_cast<CommonAPI::Proxy>( mProxy );
}

const std::unordered_map<std::string, Aws::IoTFleetWise::SomeipMethodInfo> &
MySomeipInterfaceWrapper::getSupportedActuatorInfo() const
{
    return mSupportedActuatorInfo;
}
