// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/IConnectivityModule.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include <atomic>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief A sender to send messages using AWS IoT Greengrass
 *
 * There can be multiple AwsGreengrassV2Sender from one AwsGreengrassV2ConnectivityModule. The connection of the
 * connectivityModule passed in the constructor must be established before anything meaningful
 * can be done with this class.
 * @see AwsGreengrassV2ConnectivityModule
 */
class AwsGreengrassV2Sender : public ISender
{
public:
    AwsGreengrassV2Sender( IConnectivityModule *connectivityModule,
                           Aws::Greengrass::GreengrassCoreIpcClient &greengrassClient,
                           const TopicConfig &topicConfig );
    ~AwsGreengrassV2Sender() override = default;

    AwsGreengrassV2Sender( const AwsGreengrassV2Sender & ) = delete;
    AwsGreengrassV2Sender &operator=( const AwsGreengrassV2Sender & ) = delete;
    AwsGreengrassV2Sender( AwsGreengrassV2Sender && ) = delete;
    AwsGreengrassV2Sender &operator=( AwsGreengrassV2Sender && ) = delete;

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

    const TopicConfig &
    getTopicConfig() const override
    {
        return mTopicConfig;
    }

private:
    bool isAliveNotThreadSafe();

    /** See "Message size" : "The payload for every publish request can be no larger
     * than 128 KB. AWS IoT Core rejects publish and connect requests larger than this size."
     * https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
     */
    static const size_t AWS_IOT_MAX_MESSAGE_SIZE = 131072; // = 128 KiB
    IConnectivityModule *mConnectivityModule;

    std::mutex mConnectivityMutex;
    Aws::Greengrass::GreengrassCoreIpcClient &mGreengrassClient;
    std::atomic<unsigned> mPayloadCountSent{};

    const TopicConfig &mTopicConfig;
};

} // namespace IoTFleetWise
} // namespace Aws
