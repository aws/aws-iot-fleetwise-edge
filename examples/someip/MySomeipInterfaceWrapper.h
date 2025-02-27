// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "v1/commonapi/MySomeipInterfaceProxy.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <aws/iotfleetwise/ISomeipInterfaceWrapper.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class MySomeipInterfaceWrapper : public Aws::IoTFleetWise::ISomeipInterfaceWrapper
{
public:
    MySomeipInterfaceWrapper(
        std::string domain,
        std::string instance,
        std::string connection,
        std::function<std::shared_ptr<v1::commonapi::MySomeipInterfaceProxy<>>( std::string, std::string, std::string )>
            buildProxy );

    MySomeipInterfaceWrapper( const MySomeipInterfaceWrapper & ) = delete;
    MySomeipInterfaceWrapper &operator=( const MySomeipInterfaceWrapper & ) = delete;
    MySomeipInterfaceWrapper( MySomeipInterfaceWrapper && ) = delete;
    MySomeipInterfaceWrapper &operator=( MySomeipInterfaceWrapper && ) = delete;

    bool init() override;

    std::shared_ptr<CommonAPI::Proxy> getProxy() const override;

    const std::unordered_map<std::string, Aws::IoTFleetWise::SomeipMethodInfo> &getSupportedActuatorInfo()
        const override;

private:
    std::shared_ptr<v1::commonapi::MySomeipInterfaceProxy<>> mProxy;
    std::string mDomain;
    std::string mInstance;
    std::string mConnection;
    std::function<std::shared_ptr<v1::commonapi::MySomeipInterfaceProxy<>>( std::string, std::string, std::string )>
        mBuildProxy;
    std::unordered_map<std::string, Aws::IoTFleetWise::SomeipMethodInfo> mSupportedActuatorInfo;
};
