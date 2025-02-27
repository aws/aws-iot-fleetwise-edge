// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/IConnectivityModule.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/MqttClientWrapper.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include <atomic>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief A sender to send messages using IoT Core MQTT connection
 *
 * There can be multiple AwsIotSenders from one AwsIotConnectivityModule. The connection of the
 * connectivityModule passed in the constructor must be established before anything meaningful
 * can be done with this class.
 * @see AwsIotConnectivityModule
 */
class AwsIotSender : public ISender
{
public:
    AwsIotSender( const IConnectivityModule *connectivityModule,
                  MqttClientWrapper &mqttClient,
                  const TopicConfig &topicConfig );
    ~AwsIotSender() override = default;

    AwsIotSender( const AwsIotSender & ) = delete;
    AwsIotSender &operator=( const AwsIotSender & ) = delete;
    AwsIotSender( AwsIotSender && ) = delete;
    AwsIotSender &operator=( AwsIotSender && ) = delete;

    bool isAlive() override;

    size_t getMaxSendSize() const override;

    void sendBuffer( const std::string &topic,
                     const uint8_t *buf,
                     size_t size,
                     OnDataSentCallback callback,
                     QoS qos = QoS::AT_LEAST_ONCE ) override;

    void
    invalidateConnection()
    {
        std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
        mConnectivityModule = nullptr;
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

    const TopicConfig &
    getTopicConfig() const override
    {
        return mTopicConfig;
    }

private:
    bool isAliveNotThreadSafe();

    void publishMessageToTopic( const std::string &topic,
                                const uint8_t *buf,
                                size_t size,
                                OnDataSentCallback callback,
                                Aws::Crt::Mqtt5::QOS qos );

    /** See "Message size" : "The payload for every publish request can be no larger
     * than 128 KB. AWS IoT Core rejects publish and connect requests larger than this size."
     * https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
     */
    static const size_t AWS_IOT_MAX_MESSAGE_SIZE = 131072; // = 128 KiB
    const IConnectivityModule *mConnectivityModule;
    MqttClientWrapper &mMqttClient;
    const TopicConfig &mTopicConfig;
    std::mutex mConnectivityMutex;
    std::atomic<unsigned> mPayloadCountSent{};

    /**
     * @brief Clock member variable used to generate the time an MQTT message was received
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

} // namespace IoTFleetWise
} // namespace Aws
