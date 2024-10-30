// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IConnectivityModule.h"
#include "ISender.h"
#include <atomic>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
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
                           std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &greengrassClient,
                           std::string topicName,
                           Aws::Greengrass::QOS publishQoS );
    ~AwsGreengrassV2Sender() override = default;

    AwsGreengrassV2Sender( const AwsGreengrassV2Sender & ) = delete;
    AwsGreengrassV2Sender &operator=( const AwsGreengrassV2Sender & ) = delete;
    AwsGreengrassV2Sender( AwsGreengrassV2Sender && ) = delete;
    AwsGreengrassV2Sender &operator=( AwsGreengrassV2Sender && ) = delete;

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

    std::mutex mConnectivityMutex;
    std::shared_ptr<Aws::Greengrass::GreengrassCoreIpcClient> &mGreengrassClient;
    Aws::Greengrass::QOS mPublishQoS;
    std::atomic<unsigned> mPayloadCountSent{};

    std::string mTopicName;
};

} // namespace IoTFleetWise
} // namespace Aws
