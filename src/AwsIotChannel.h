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
#include "MqttClientWrapper.h"
#include "PayloadManager.h"
#include <atomic>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief a channel that can be used as IReceiver or ISender or both
 *
 * If the Channel should be used for receiving data subscribe must be called.
 * setTopic must be called always. There can be multiple AwsIotChannels
 * from one AwsIotConnectivityModule. The channel of the connectivityModule passed in the
 * constructor must be established before anything meaningful can be done with this class
 * @see AwsIotConnectivityModule
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class AwsIotChannel : public IConnectivityChannel
{
public:
    AwsIotChannel( IConnectivityModule *connectivityModule,
                   std::shared_ptr<PayloadManager> payloadManager,
                   std::shared_ptr<MqttClientWrapper> &mqttClient,
                   std::string topicName,
                   bool subscription );
    ~AwsIotChannel() override;

    AwsIotChannel( const AwsIotChannel & ) = delete;
    AwsIotChannel &operator=( const AwsIotChannel & ) = delete;
    AwsIotChannel( AwsIotChannel && ) = delete;
    AwsIotChannel &operator=( AwsIotChannel && ) = delete;

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

    /**
     * @brief Unsubscribe from the MQTT topic asynchronously
     * @return A future that can be used to wait for the unsubscribe to finish. It will return True on success.
     */
    std::future<bool> unsubscribeAsync();

    bool isAlive() override;

    size_t getMaxSendSize() const override;

    ConnectivityError sendBuffer( const std::uint8_t *buf,
                                  size_t size,
                                  CollectionSchemeParams collectionSchemeParams = CollectionSchemeParams() ) override;

    ConnectivityError sendFile( const std::string &filePath,
                                size_t size,
                                CollectionSchemeParams collectionSchemeParams = CollectionSchemeParams() ) override;

    void subscribeToDataReceived( OnDataReceivedCallback callback ) override;

    void onDataReceived( const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData );

    void
    invalidateConnection()
    {
        std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
        std::lock_guard<std::mutex> connectivityLambdaLock( mConnectivityLambdaMutex );
        mConnectivityModule = nullptr;
    };

    bool
    shouldSubscribeAsynchronously() const
    {
        return mSubscription;
    };

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

    // coverity[autosar_cpp14_a0_1_3_violation] false positive - function is used
    bool
    isTopicValid()
    {
        return !mTopicName.empty();
    };

    void publishMessage( const uint8_t *buf, size_t size );

    /** See "Message size" : "The payload for every publish request can be no larger
     * than 128 KB. AWS IoT Core rejects publish and connect requests larger than this size."
     * https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
     */
    static const size_t AWS_IOT_MAX_MESSAGE_SIZE = 131072; // = 128 KiB
    IConnectivityModule *mConnectivityModule;
    ThreadSafeListeners<OnDataReceivedCallback> mListeners;
    std::shared_ptr<PayloadManager> mPayloadManager;
    std::shared_ptr<MqttClientWrapper> &mMqttClient;
    std::mutex mConnectivityMutex;
    std::mutex mConnectivityLambdaMutex;
    std::string mTopicName;
    std::atomic<bool> mSubscribed;
    std::atomic<unsigned> mPayloadCountSent{};

    /**
     * @brief Clock member variable used to generate the time an MQTT message was received
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    bool mSubscription;
};

} // namespace IoTFleetWise
} // namespace Aws
