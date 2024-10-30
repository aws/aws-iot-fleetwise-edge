// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "IConnectionTypes.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "Listener.h"
#include <atomic>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

using SubscribeCallback = std::function<void( const ReceivedConnectivityMessage &receivedMessage )>;

class SubscribeStreamHandler : public Aws::Greengrass::SubscribeToIoTCoreStreamHandler
{
public:
    SubscribeStreamHandler( SubscribeCallback callback )
        : mCallback( std::move( callback ) )
    {
    }
    virtual ~SubscribeStreamHandler() = default;

private:
    // coverity[autosar_cpp14_a0_1_3_violation] false positive - function overrides sdk's virtual function.
    void OnStreamEvent( Aws::Greengrass::IoTCoreMessage *response ) override;

    SubscribeCallback mCallback;

    /**
     * @brief Clock member variable used to generate the time an MQTT message was received
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

/**
 * @brief A receiver to receive messages using AWS IoT Greengrass
 *
 * There can be multiple AwsGreengrassV2Receiver from one AwsGreengrassV2ConnectivityModule. The connection of the
 * connectivityModule passed in the constructor must be established before anything meaningful
 * can be done with this class.
 * @see AwsGreengrassV2ConnectivityModule
 */
class AwsGreengrassV2Receiver : public IReceiver
{
public:
    AwsGreengrassV2Receiver( IConnectivityModule *connectivityModule,
                             std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &greengrassClient,
                             std::string topicName );
    ~AwsGreengrassV2Receiver() override;

    AwsGreengrassV2Receiver( const AwsGreengrassV2Receiver & ) = delete;
    AwsGreengrassV2Receiver &operator=( const AwsGreengrassV2Receiver & ) = delete;
    AwsGreengrassV2Receiver( AwsGreengrassV2Receiver && ) = delete;
    AwsGreengrassV2Receiver &operator=( AwsGreengrassV2Receiver && ) = delete;

    /**
     * @brief Subscribe to the MQTT topic from setTopic. Necessary if data is received on the topic
     *
     * This function blocks until subscribe succeeded or failed and should be done in the setup form
     * the bootstrap thread. The connection of the connectivityModule passed in the constructor
     * must be established otherwise subscribe will fail. No retries are done to try to subscribe
     * this needs to be done in the bootstrap during the setup.
     * @return Success if subscribe finished correctly
     */
    ConnectivityError subscribe();

    /**
     * @brief After unsubscribe no data will be received by the receiver
     * @return True for success
     */
    bool unsubscribe();

    bool isAlive() override;

    void subscribeToDataReceived( OnDataReceivedCallback callback ) override;

    void
    invalidateConnection()
    {
        std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
        mConnectivityModule = nullptr;
    }

private:
    bool isAliveNotThreadSafe();

    IConnectivityModule *mConnectivityModule;
    ThreadSafeListeners<OnDataReceivedCallback> mListeners;

    std::mutex mConnectivityMutex;
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &mGreengrassClient;
    std::atomic<bool> mSubscribed;

    std::string mTopicName;
    std::shared_ptr<SubscribeStreamHandler> mSubscribeStreamHandler;
    std::shared_ptr<Aws::Greengrass::SubscribeToIoTCoreOperation> mSubscribeOperation;
};

} // namespace IoTFleetWise
} // namespace Aws
