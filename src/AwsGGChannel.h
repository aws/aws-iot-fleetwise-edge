// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "IConnectionTypes.h"
#include "IConnectivityChannel.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "ISender.h"
#include "Listener.h"
#include "PayloadManager.h"
#include <atomic>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

using SubscribeCallback = std::function<void( const ReceivedChannelMessage &receivedChannelMessage )>;

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
 * @brief a channel that can be used as IReceiver or ISender or both
 *
 * If the Channel should be used for receiving data subscribe must be called.
 * setTopic must be called always. There can be multiple AwsGGChannels
 * from one AwsGGConnectivityModule. The channel of the connectivityModule passed in the
 * constructor must be established before anything meaningful can be done with this class
 * @see AwsGGConnectivityModule
 */
class AwsGGChannel : public IConnectivityChannel
{
public:
    AwsGGChannel( IConnectivityModule *connectivityModule,
                  std::shared_ptr<PayloadManager> payloadManager,
                  std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &ggConnection,
                  std::string topicName,
                  bool subscription );
    ~AwsGGChannel() override;

    AwsGGChannel( const AwsGGChannel & ) = delete;
    AwsGGChannel &operator=( const AwsGGChannel & ) = delete;
    AwsGGChannel( AwsGGChannel && ) = delete;
    AwsGGChannel &operator=( AwsGGChannel && ) = delete;

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
     * @brief After unsubscribe no data will be received over the channel
     * @return True for success
     */
    bool unsubscribe();

    bool isAlive() override;

    size_t getMaxSendSize() const override;

    ConnectivityError sendBuffer( const std::uint8_t *buf,
                                  size_t size,
                                  CollectionSchemeParams collectionSchemeParams = CollectionSchemeParams() ) override;

    ConnectivityError sendFile( const std::string &filePath,
                                size_t size,
                                CollectionSchemeParams collectionSchemeParams = CollectionSchemeParams() ) override;

    void subscribeToDataReceived( OnDataReceivedCallback callback ) override;

    void
    invalidateConnection()
    {
        std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
        std::lock_guard<std::mutex> connectivityLambdaLock( mConnectivityLambdaMutex );
        mConnectivityModule = nullptr;
    }

    bool
    shouldSubscribeAsynchronously() const
    {
        return mSubscription;
    }

    /**
     * @brief Returns the number of payloads successfully passed to the AWS IoT SDK
     * @return Number of payloads
     */
    unsigned
    getPayloadCountSent() const override
    {
        return mPayloadCountSent;
    }

private:
    bool isAliveNotThreadSafe();
    bool
    isTopicValid()
    {
        return !mTopicName.empty();
    };
    /** See "Message size" : "The payload for every publish request can be no larger
     * than 128 KB. AWS IoT Core rejects publish and connect requests larger than this size."
     * https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
     */
    static const size_t AWS_IOT_MAX_MESSAGE_SIZE = 131072; // = 128 KiB
    IConnectivityModule *mConnectivityModule;
    ThreadSafeListeners<OnDataReceivedCallback> mListeners;

    std::mutex mConnectivityMutex;
    std::mutex mConnectivityLambdaMutex;
    std::shared_ptr<PayloadManager> mPayloadManager;
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &mConnection;
    std::atomic<bool> mSubscribed;
    bool mSubscription;
    std::atomic<unsigned> mPayloadCountSent{};

    std::string mTopicName;
    std::shared_ptr<SubscribeStreamHandler> mSubscribeStreamHandler;
    std::shared_ptr<Aws::Greengrass::SubscribeToIoTCoreOperation> mSubscribeOperation;
};

} // namespace IoTFleetWise
} // namespace Aws
