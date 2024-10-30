// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AwsGreengrassV2Receiver.h"
#include "AwsGreengrassV2Sender.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "ISender.h"
#include "Listener.h"
#include "LoggingModule.h"
#include <atomic>
#include <aws/crt/io/Bootstrap.h>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class IpcLifecycleHandler : public ConnectionLifecycleHandler
{
public:
    virtual ~IpcLifecycleHandler() = default;

private:
    // coverity[autosar_cpp14_a0_1_3_violation] false positive - function overrides sdk's virtual function.
    void
    OnConnectCallback() override
    {
        FWE_LOG_INFO( "Connected to Greengrass Core." );
    }

    // coverity[autosar_cpp14_a0_1_3_violation] false positive - function overrides sdk's virtual function.
    void
    OnDisconnectCallback( RpcError status ) override
    {
        if ( !status )
        {
            FWE_LOG_ERROR( "Disconnected from Greengrass Core with error: " + std::to_string( status ) );
        }
    }

    // coverity[autosar_cpp14_a0_1_3_violation] false positive - function overrides sdk's virtual function.
    bool
    OnErrorCallback( RpcError status ) override
    {
        FWE_LOG_ERROR( "Processing messages from the Greengrass Core resulted in error:" + std::to_string( status ) );
        return true;
    }
};

/**
 * @brief bootstrap of the AWS IoT Greengrass connectivity module. Only one object of this should normally exist
 *
 */
class AwsGreengrassV2ConnectivityModule : public IConnectivityModule
{
public:
    AwsGreengrassV2ConnectivityModule( Aws::Crt::Io::ClientBootstrap *clientBootstrap );
    ~AwsGreengrassV2ConnectivityModule() override;

    AwsGreengrassV2ConnectivityModule( const AwsGreengrassV2ConnectivityModule & ) = delete;
    AwsGreengrassV2ConnectivityModule &operator=( const AwsGreengrassV2ConnectivityModule & ) = delete;
    AwsGreengrassV2ConnectivityModule( AwsGreengrassV2ConnectivityModule && ) = delete;
    AwsGreengrassV2ConnectivityModule &operator=( AwsGreengrassV2ConnectivityModule && ) = delete;

    bool connect() override;

    bool disconnect() override;

    bool
    isAlive() const override
    {
        return mConnected;
    };

    std::shared_ptr<ISender> createSender( const std::string &topicName, QoS publishQoS = QoS::AT_MOST_ONCE ) override;

    std::shared_ptr<IReceiver> createReceiver( const std::string &topicName ) override;

    void subscribeToConnectionEstablished( OnConnectionEstablishedCallback callback ) override;

private:
    std::atomic<bool> mConnected;
    std::atomic<bool> mConnectionEstablished;
    ThreadSafeListeners<OnConnectionEstablishedCallback> mConnectionEstablishedListeners;
    std::vector<std::shared_ptr<AwsGreengrassV2Sender>> mSenders;
    std::vector<std::shared_ptr<AwsGreengrassV2Receiver>> mReceivers;
    std::unique_ptr<IpcLifecycleHandler> mLifecycleHandler;
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> mGreengrassClient;
    Aws::Crt::Io::ClientBootstrap *mClientBootstrap;
};

} // namespace IoTFleetWise
} // namespace Aws
