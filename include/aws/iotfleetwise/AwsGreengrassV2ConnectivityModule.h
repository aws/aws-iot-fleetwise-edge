// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/AwsGreengrassCoreIpcClientWrapper.h"
#include "aws/iotfleetwise/AwsGreengrassV2Receiver.h"
#include "aws/iotfleetwise/AwsGreengrassV2Sender.h"
#include "aws/iotfleetwise/IConnectivityModule.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/Listener.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include <atomic>
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

    void
    OnConnectCallback() override
    {
        FWE_LOG_INFO( "Connected to Greengrass Core." );
    }

    void
    OnDisconnectCallback( RpcError status ) override
    {
        if ( !status )
        {
            FWE_LOG_ERROR( "Disconnected from Greengrass Core with error: " + std::to_string( status ) );
        }
        else
        {
            FWE_LOG_INFO( "Disconnected from Greengrass Core." );
        }
    }

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
    AwsGreengrassV2ConnectivityModule( AwsGreengrassCoreIpcClientWrapper &greengrassClientWrapper,
                                       const TopicConfig &topicConfig );
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

    std::shared_ptr<ISender> createSender() override;

    std::shared_ptr<IReceiver> createReceiver( const std::string &topicName ) override;

    void subscribeToConnectionEstablished( OnConnectionEstablishedCallback callback ) override;

private:
    std::atomic<bool> mConnected;
    std::atomic<bool> mConnectionEstablished;
    ThreadSafeListeners<OnConnectionEstablishedCallback> mConnectionEstablishedListeners;
    std::vector<std::shared_ptr<AwsGreengrassV2Sender>> mSenders;
    std::vector<std::shared_ptr<AwsGreengrassV2Receiver>> mReceivers;
    IpcLifecycleHandler mLifecycleHandler;
    AwsGreengrassCoreIpcClientWrapper &mGreengrassClientWrapper;
    const TopicConfig &mTopicConfig;
};

} // namespace IoTFleetWise
} // namespace Aws
