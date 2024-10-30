// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AwsIotReceiver.h"
#include "AwsIotSender.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "ISender.h"
#include "Listener.h"
#include "LoggingModule.h"
#include "MqttClientWrapper.h"
#include "RetryThread.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

// Default MQTT Keep alive in seconds.
// Defines how often an MQTT PING message is sent to the MQTT broker to keep the connection alive
// Default set to 60 seconds. Every 60 seconds the stack will send an MQTT PING req.
// The longer this interval is, the more the stack takes to detect the state of the TCP connection
// at the lower network layers.
// This parameter is asserted in the C SDK. It shall be strictly bigger than the default
// connection ping timeout.
// Refer to https://github.com/awslabs/aws-c-mqtt/blob/a2ee9a321fcafa19b0473b88a54e0ae8dde5fddf/source/client.c#L1461
// This default is the same as IoT Core's default. If you need to configure this, override it in the config file.
constexpr uint16_t MQTT_KEEP_ALIVE_INTERVAL_SECONDS = 1200;
// Default ping timeout value in milliseconds
// If a response is not received within this interval, the connection will be reestablished.
// If the PING request does not return within this interval, the stack will create a new one.
// This default is the same as aws-c-mqtt's default:
// https://github.com/awslabs/aws-c-mqtt/blob/a2ee9a321fcafa19b0473b88a54e0ae8dde5fddf/include/aws/mqtt/private/v5/mqtt5_utils.h#L82
// If you need to configure this, override it in the config file.
constexpr uint32_t MQTT_PING_TIMEOUT_MS = 30000;
// Default expiry interval for persistent sessions. If 0, persistent sessions will be disabled.
constexpr uint32_t MQTT_SESSION_EXPIRY_INTERVAL_SECONDS = 0;

// How much time to wait for a response to an unsubscribe operation when shutting the module down.
constexpr uint32_t MQTT_UNSUBSCRIBE_TIMEOUT_ON_SHUTDOWN_SECONDS = 5;

struct AwsIotConnectivityConfig
{
    uint16_t keepAliveIntervalSeconds = MQTT_KEEP_ALIVE_INTERVAL_SECONDS;
    uint32_t pingTimeoutMs = MQTT_PING_TIMEOUT_MS;
    uint32_t sessionExpiryIntervalSeconds = MQTT_SESSION_EXPIRY_INTERVAL_SECONDS;
};

struct TopicNode
{
    std::shared_ptr<AwsIotReceiver> mReceiver;
    std::unordered_map<std::string, std::unique_ptr<TopicNode>> mChildren;
};

/**
 * @brief Map topic filters, including wildcards, to receivers.
 *
 * This is necessary to provide a generic way to subscribe to topics containing wildcards.
 *
 * Differently from v3, the MQTT v5 client doesn't support setting callbacks per subscription. There
 * is only one callback that receives all Publish packets and we need to do the routing ourselves.
 *
 * Since we need to use wildcards, we can't use a simple topic->receiver map. We need to split the
 * topics and build a tree so that we can match level by level.
 *
 * For some related discussion, see https://github.com/aws/aws-iot-device-sdk-java-v2/issues/453
 */
class TopicTree
{
public:
    void
    insert( const std::string &topic, std::shared_ptr<AwsIotReceiver> receiver )
    {
        size_t start = 0;
        size_t foundPos = topic.find( '/', start );
        TopicNode *currentNode = nullptr;
        auto *currentChildren = &mChildren;
        while ( true )
        {
            size_t n = foundPos == std::string::npos ? foundPos : foundPos - start;
            std::string topicLevel = topic.substr( start, n );
            currentNode = currentChildren->emplace( topicLevel, std::make_unique<TopicNode>() ).first->second.get();
            currentChildren = &currentNode->mChildren;

            if ( foundPos == std::string::npos )
            {
                break;
            }

            start = foundPos + 1;
            foundPos = topic.find( '/', start );
        }

        if ( currentNode->mReceiver != nullptr )
        {
            FWE_LOG_ERROR( "Topic already exists: " + topic );
            return;
        }

        currentNode->mReceiver = receiver;
    }

    std::shared_ptr<AwsIotReceiver>
    find( const std::string &topic )
    {
        size_t start = 0;
        size_t foundPos = topic.find( '/', start );
        TopicNode *currentNode = nullptr;
        auto *currentChildren = &mChildren;
        while ( true )
        {
            size_t n = foundPos == std::string::npos ? foundPos : foundPos - start;
            std::string topicLevel = topic.substr( start, n );
            auto it = currentChildren->find( topicLevel );
            // If we don't find an exact match, we need to check whether there is a wildcard
            if ( it == currentChildren->end() )
            {
                it = currentChildren->find( "+" );
                if ( it == currentChildren->end() )
                {
                    it = currentChildren->find( "#" );
                    if ( it == currentChildren->end() )
                    {
                        FWE_LOG_ERROR( "Topic level does not exist: " + topicLevel );
                        return nullptr;
                    }
                }
            }

            currentNode = it->second.get();
            currentChildren = &currentNode->mChildren;

            if ( ( foundPos == std::string::npos ) || ( it->first == "#" ) )
            {
                break;
            }

            start = foundPos + 1;
            foundPos = topic.find( '/', start );
        }
        return currentNode->mReceiver;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<TopicNode>> mChildren;
};

/**
 * @brief bootstrap of the Aws Iot SDK. Only one object of this should normally exist
 * */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class AwsIotConnectivityModule : public IConnectivityModule
{
public:
    constexpr static uint32_t RETRY_FIRST_CONNECTION_START_BACKOFF_MS = 1000; // start retry after one second
    constexpr static uint32_t RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS = 256000; // retry at least every 256 seconds

    /**
     * @brief Construct a new Aws Iot Connectivity Module object
     *
     * @param rootCA The Root CA for the certificate
     * @param clientId the id that is used to identify this connection instance
     * @param mqttClientBuilder a builder that can create MQTT client instances
     * @param connectionConfig allows some connection config to be overriden
     */
    AwsIotConnectivityModule( std::string rootCA,
                              std::string clientId,
                              std::shared_ptr<MqttClientBuilderWrapper> mqttClientBuilder,
                              AwsIotConnectivityConfig connectionConfig = AwsIotConnectivityConfig() );
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

    std::shared_ptr<ISender> createSender( const std::string &topicName, QoS publishQoS = QoS::AT_MOST_ONCE ) override;

    std::shared_ptr<IReceiver> createReceiver( const std::string &topicName ) override;

    void subscribeToConnectionEstablished( OnConnectionEstablishedCallback callback ) override;

private:
    bool createMqttClient();
    RetryStatus connectMqttClient();
    RetryStatus subscribeAllReceivers();
    static void renameEventLoopTask();
    bool resetConnection();

    std::string mRootCA;
    std::string mClientId;
    std::shared_ptr<MqttClientWrapper> mMqttClient;
    std::shared_ptr<MqttClientBuilderWrapper> mMqttClientBuilder;
    AwsIotConnectivityConfig mConnectionConfig;
    RetryThread mInitialConnectionThread;
    RetryThread mSubscriptionsThread;

    std::promise<bool> mConnectionCompletedPromise;
    std::promise<void> mConnectionClosedPromise;
    std::atomic<bool> mConnected;
    std::atomic<bool> mConnectionEstablished;
    ThreadSafeListeners<OnConnectionEstablishedCallback> mConnectionEstablishedListeners;

    std::vector<std::shared_ptr<AwsIotSender>> mSenders;
    std::vector<std::shared_ptr<AwsIotReceiver>> mReceivers;
    std::unordered_map<std::string, std::shared_ptr<AwsIotReceiver>> mSubscribedTopicToReceiver;
    TopicTree mSubscribedTopicsTree;
    std::mutex mTopicsMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
