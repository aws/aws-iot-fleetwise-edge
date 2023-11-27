// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AwsIotChannel.h"
#include "IConnectivityChannel.h"
#include "IConnectivityModule.h"
#include "MqttClientWrapper.h"
#include "PayloadManager.h"
#include "RetryThread.h"
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief bootstrap of the Aws Iot SDK. Only one object of this should normally exist
 * */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class AwsIotConnectivityModule : public IRetryable, public IConnectivityModule
{
public:
    constexpr static uint32_t RETRY_FIRST_CONNECTION_START_BACKOFF_MS = 1000; // start retry after one second
    constexpr static uint32_t RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS = 256000; // retry at least every 256 seconds

    /**
     * @brief Construct a new Aws Iot Connectivity Module object
     *
     * @param rootCA The Root CA for the certificate
     * @param clientId the id that is used to identify this connection instance
     * @param mqttClientBuilder a buider that can create MQTT client instances
     */
    AwsIotConnectivityModule( std::string rootCA,
                              std::string clientId,
                              std::shared_ptr<MqttClientBuilderWrapper> mqttClientBuilder );
    ~AwsIotConnectivityModule() override;

    AwsIotConnectivityModule( const AwsIotConnectivityModule & ) = delete;
    AwsIotConnectivityModule &operator=( const AwsIotConnectivityModule & ) = delete;
    AwsIotConnectivityModule( AwsIotConnectivityModule && ) = delete;
    AwsIotConnectivityModule &operator=( AwsIotConnectivityModule && ) = delete;

    /**
     * @brief Connect to a "Thing" from the AWS IoT Core
     *
     * A thing created over the Aws CLI under IoT Core normally has a security and a collectionScheme
     * attached. The endpoint of the thing must be passed as parameter.
     * This function should be called only once on the object
     * @return True if connecting was successful in the synchronous case or if asynchronous true if the establish
     * connection retry thread was successfully started
     */
    bool connect() override;

    bool disconnect() override;

    bool
    isAlive() const override
    {
        return mConnected;
    };

    RetryStatus attempt() override;

    void onFinished( RetryStatus code ) override;

    /**
     * @brief create a new channel sharing the connection of this module
     * This call needs to be done before calling connect for all asynchronous subscribe channel
     * @param payloadManager the payload manager used by the new channel,
     * sending data
     * @param topicName the topic which this channel should subscribe/publish to
     * @param subscription whether the channel should subscribe to the topic. Otherwise it will
     * just publish to it.
     *
     * @return a pointer to the newly created channel. A reference to the newly created channel is also hold inside this
     * module.
     */
    std::shared_ptr<IConnectivityChannel> createNewChannel( const std::shared_ptr<PayloadManager> &payloadManager,
                                                            const std::string &topicName,
                                                            bool subscription = false ) override;

private:
    bool createMqttConnection();
    static void renameEventLoopTask();
    bool resetConnection();

    std::string mRootCA;
    std::string mClientId;
    std::shared_ptr<MqttClientWrapper> mMqttClient;
    std::shared_ptr<MqttClientBuilderWrapper> mMqttClientBuilder;
    RetryThread mRetryThread;

    std::promise<bool> mConnectionCompletedPromise;
    std::promise<void> mConnectionClosedPromise;
    std::atomic<bool> mConnected;
    std::atomic<bool> mConnectionEstablished;

    std::vector<std::shared_ptr<AwsIotChannel>> mChannels;
    std::unordered_map<std::string, std::shared_ptr<AwsIotChannel>> mTopicToChannel;
    std::mutex mTopicToChannelMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
