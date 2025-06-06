// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/Listener.h"
#include "aws/iotfleetwise/MqttClientWrapper.h"
#include <atomic>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief A receiver to receive messages using IoT Core MQTT connection
 *
 * There can be multiple AwsIotReceivers from one AwsIotConnectivityModule. The connection of the
 * connectivityModule passed in the constructor must be established before anything meaningful
 * can be done with this class.
 * @see AwsIotConnectivityModule
 */
class AwsIotReceiver : public IReceiver
{
public:
    AwsIotReceiver( MqttClientWrapper &mqttClient, std::string topicName );
    ~AwsIotReceiver() override;

    AwsIotReceiver( const AwsIotReceiver & ) = delete;
    AwsIotReceiver &operator=( const AwsIotReceiver & ) = delete;
    AwsIotReceiver( AwsIotReceiver && ) = delete;
    AwsIotReceiver &operator=( AwsIotReceiver && ) = delete;

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
     * @brief After unsubscribe no data will be received over by the receiver
     * @return True for success
     */
    bool unsubscribe();

    /**
     * @brief Unsubscribe from the MQTT topic asynchronously
     * @return A future that can be used to wait for the unsubscribe to finish. It will return True on success.
     */
    std::future<bool> unsubscribeAsync();

    void subscribeToDataReceived( OnDataReceivedCallback callback ) override;

    void onDataReceived( const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData );

    void
    resetSubscription()
    {
        mSubscribed = false;
    }

private:
    ThreadSafeListeners<OnDataReceivedCallback> mListeners;
    MqttClientWrapper &mMqttClient;
    std::mutex mConnectivityMutex;
    std::string mTopicName;
    std::atomic<bool> mSubscribed;

    /**
     * @brief Clock member variable used to generate the time an MQTT message was received
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

} // namespace IoTFleetWise
} // namespace Aws
