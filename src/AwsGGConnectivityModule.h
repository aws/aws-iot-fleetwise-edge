// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AwsGGChannel.h"
#include "IConnectivityChannel.h"
#include "IConnectivityModule.h"
#include "LoggingModule.h"
#include "PayloadManager.h"
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
 * @brief bootstrap of the Aws GG connectivity module. Only one object of this should normally exist
 *
 */
class AwsGGConnectivityModule : public IConnectivityModule
{
public:
    AwsGGConnectivityModule( Aws::Crt::Io::ClientBootstrap *clientBootstrap );
    ~AwsGGConnectivityModule() override;

    AwsGGConnectivityModule( const AwsGGConnectivityModule & ) = delete;
    AwsGGConnectivityModule &operator=( const AwsGGConnectivityModule & ) = delete;
    AwsGGConnectivityModule( AwsGGConnectivityModule && ) = delete;
    AwsGGConnectivityModule &operator=( AwsGGConnectivityModule && ) = delete;

    bool connect() override;

    bool disconnect() override;

    bool
    isAlive() const override
    {
        return mConnected;
    };

    /**
     * @brief create a new channel sharing the connection of this module
     * This call needs to be done before calling connect for all asynchronous subscribe channel
     * @param payloadManager the payload manager used by the new channel,
     * @return a pointer to the newly created channel. A reference to the newly created channel is also hold inside this
     * module.
     */
    std::shared_ptr<IConnectivityChannel> createNewChannel(
        const std::shared_ptr<PayloadManager> &payloadManager ) override;

private:
    std::atomic<bool> mConnected;
    std::atomic<bool> mConnectionEstablished;
    std::vector<std::shared_ptr<AwsGGChannel>> mChannels;
    std::unique_ptr<IpcLifecycleHandler> mLifecycleHandler;
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> mConnection;
    Aws::Crt::Io::ClientBootstrap *mClientBootstrap;
};

} // namespace IoTFleetWise
} // namespace Aws
