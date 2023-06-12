// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "IReceiver.h"
#include "ISender.h"
#include "PayloadManager.h"
#include <atomic>
#include <aws/crt/Api.h>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
/**
 * @brief Namespace depending on Aws Iot SDK headers
 */
namespace OffboardConnectivityAwsIot
{

class IConnectivityModule
{
public:
    virtual std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> getConnection() const = 0;

    /**
     * @brief Increases atomically the memory usage
     * @param bytes number of bytes to reserve
     * @return number of bytes after the increase.
     */
    virtual std::size_t reserveMemoryUsage( std::size_t bytes ) = 0;

    /**
     * @brief Decreases atomically the memory usage
     * @param bytes number of bytes to release
     * @return number of bytes after the decrease.
     */
    virtual std::size_t releaseMemoryUsage( std::size_t bytes ) = 0;

    virtual bool isAlive() const = 0;

    virtual ~IConnectivityModule() = default;
};

using Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams;
using Aws::IoTFleetWise::OffboardConnectivity::ConnectivityError;

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
class AwsIotChannel : public Aws::IoTFleetWise::OffboardConnectivity::ISender,
                      public Aws::IoTFleetWise::OffboardConnectivity::IReceiver
{
public:
    static constexpr std::size_t MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES =
        10000000; /**< After the SDK allocated more than the here defined 10MB we will stop pushing data to the SDK to
                     avoid increasing heap consumption */

    AwsIotChannel( IConnectivityModule *connectivityModule,
                   std::shared_ptr<PayloadManager> payloadManager,
                   std::size_t maximumIotSDKHeapMemoryBytes = MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES );
    ~AwsIotChannel() override;

    AwsIotChannel( const AwsIotChannel & ) = delete;
    AwsIotChannel &operator=( const AwsIotChannel & ) = delete;
    AwsIotChannel( AwsIotChannel && ) = delete;
    AwsIotChannel &operator=( AwsIotChannel && ) = delete;

    /**
     * @brief the topic must be set always before using any functionality of this class
     * @param topicNameRef MQTT topic that will be used for sending or receiving data
     *                      if subscribe was called
     * @param subscribeAsynchronously if true the channel will be subscribed to the topic asynchronously so that the
     * channel can receive data
     *
     */
    void setTopic( const std::string &topicNameRef, bool subscribeAsynchronously = false );

    /**
     * @brief Subscribe to the MQTT topic from setTopic. Necessary if data is received on the topic
     *
     * This function blocks until subscribe succeeded or failed and should be done in the setup form
     * the bootstrap thread. The connection of the connectivityModule passed in the constructor
     * must be established otherwise subscribe will fail. No retries are done to try to subscribe
     * this needs to be done in the boostrap during the setup.
     * @return Success if subscribe finished correctly
     */
    ConnectivityError subscribe();

    /**
     * @brief After unsubscribe no data will be received over the channel
     */
    bool unsubscribe();

    bool isAlive() override;

    size_t getMaxSendSize() const override;

    ConnectivityError sendBuffer(
        const std::uint8_t *buf,
        size_t size,
        struct CollectionSchemeParams collectionSchemeParams = CollectionSchemeParams() ) override;

    bool
    isTopicValid()
    {
        return !mTopicName.empty();
    }

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
        return mSubscribeAsynchronously;
    }

    /**
     * @brief Returns the number of payloads successfully passed to the AWS IoT SDK
     * @return Number of payloads
     */
    unsigned
    getPayloadCountSent() const
    {
        return mPayloadCountSent;
    }

private:
    bool isAliveNotThreadSafe();

    /** See "Message size" : "The payload for every publish request can be no larger
     * than 128 KB. AWS IoT Core rejects publish and connect requests larger than this size."
     * https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
     */
    static const size_t AWS_IOT_MAX_MESSAGE_SIZE = 131072; // = 128 KiB
    std::size_t mMaximumIotSDKHeapMemoryBytes; /**< If the iot device sdk heap memory usage from all channels exceeds
                                               this threshold this channel stops publishing data*/
    IConnectivityModule *mConnectivityModule;
    std::mutex mConnectivityMutex;
    std::mutex mConnectivityLambdaMutex;
    std::shared_ptr<PayloadManager> mPayloadManager;
    std::string mTopicName;
    std::atomic<bool> mSubscribed;
    std::atomic<unsigned> mPayloadCountSent{};

    bool mSubscribeAsynchronously;
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws
