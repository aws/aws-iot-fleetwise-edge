// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "IConnectivityModule.h"
#include "ISender.h"
#include "MqttClientWrapper.h"
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
    AwsIotSender( IConnectivityModule *connectivityModule,
                  std::shared_ptr<MqttClientWrapper> &mqttClient,
                  std::string topicName,
                  Aws::Crt::Mqtt5::QOS publishQoS );
    ~AwsIotSender() override = default;

    AwsIotSender( const AwsIotSender & ) = delete;
    AwsIotSender &operator=( const AwsIotSender & ) = delete;
    AwsIotSender( AwsIotSender && ) = delete;
    AwsIotSender &operator=( AwsIotSender && ) = delete;

    bool isAlive() override;

    size_t getMaxSendSize() const override;

    void sendBuffer( const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) override;

    void sendBufferToTopic( const std::string &topic,
                            const uint8_t *buf,
                            size_t size,
                            OnDataSentCallback callback ) override;

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

private:
    bool isAliveNotThreadSafe();

    // coverity[autosar_cpp14_a0_1_3_violation] false positive - function is used
    bool
    isTopicValid()
    {
        return !mTopicName.empty();
    };

    void publishMessageToTopic( const std::string &topic,
                                const uint8_t *buf,
                                size_t size,
                                OnDataSentCallback callback );

    /** See "Message size" : "The payload for every publish request can be no larger
     * than 128 KB. AWS IoT Core rejects publish and connect requests larger than this size."
     * https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
     */
    static const size_t AWS_IOT_MAX_MESSAGE_SIZE = 131072; // = 128 KiB
    IConnectivityModule *mConnectivityModule;
    std::shared_ptr<MqttClientWrapper> &mMqttClient;
    std::mutex mConnectivityMutex;
    std::string mTopicName;
    std::atomic<unsigned> mPayloadCountSent{};

    /**
     * @brief Clock member variable used to generate the time an MQTT message was received
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    Aws::Crt::Mqtt5::QOS mPublishQoS;
};

} // namespace IoTFleetWise
} // namespace Aws