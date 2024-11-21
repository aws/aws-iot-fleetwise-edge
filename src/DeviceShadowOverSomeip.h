// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IReceiver.h"
#include "ISender.h"
#include "v1/commonapi/DeviceShadowOverSomeipInterface.hpp"
#include "v1/commonapi/DeviceShadowOverSomeipInterfaceStubDefault.hpp"
#include <CommonAPI/CommonAPI.hpp> // IWYU pragma: keep
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

class DeviceShadowOverSomeip : public v1_0::commonapi::DeviceShadowOverSomeipInterfaceStubDefault
{
public:
    DeviceShadowOverSomeip( std::shared_ptr<ISender> sender );
    ~DeviceShadowOverSomeip() override = default;

    DeviceShadowOverSomeip( const DeviceShadowOverSomeip & ) = delete;
    DeviceShadowOverSomeip &operator=( const DeviceShadowOverSomeip & ) = delete;
    DeviceShadowOverSomeip( DeviceShadowOverSomeip && ) = delete;
    DeviceShadowOverSomeip &operator=( DeviceShadowOverSomeip && ) = delete;

    void getShadow( const std::shared_ptr<CommonAPI::ClientId> _client,
                    std::string _shadowName,
                    getShadowReply_t _reply ) override;

    void updateShadow( const std::shared_ptr<CommonAPI::ClientId> _client,
                       std::string _shadowName,
                       std::string _updateDocument,
                       updateShadowReply_t _reply ) override;

    void deleteShadow( const std::shared_ptr<CommonAPI::ClientId> _client,
                       std::string _shadowName,
                       deleteShadowReply_t _reply ) override;

    void onDataReceived( const ReceivedConnectivityMessage &receivedMessage );

private:
    std::shared_ptr<ISender> mMqttSender;
    std::string mClientTokenRandomPrefix;
    std::atomic<int> mClientTokenCounter{};
    using ResponseCallback = std::function<void( //
        v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode,
        const std::string &errorMessage,
        const std::string &responseDocument )>;
    std::unordered_map<std::string, ResponseCallback> mRequestTable;
    std::mutex mRequestTableMutex;
    void sendRequest( const std::string &topic, const std::string &requestDocument, ResponseCallback callback );
};

} // namespace IoTFleetWise
} // namespace Aws
