// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "AwsIotChannel.h"
#include "Listener.h"
#include "RetryThread.h"
#include <atomic>
#include <cstddef>
#include <future>
#include <string>

#include <aws/crt/Api.h>

#include <aws/iot/MqttClient.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{
using namespace Aws::IoTFleetWise::Platform::Linux;

/**
 * @brief bootstrap of the Aws Iot SDK. Only one object of this should normally exist
 * */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
class AwsIotConnectivityModule : public IRetryable, public IConnectivityModule
{
public:
    constexpr static uint32_t RETRY_FIRST_CONNECTION_START_BACKOFF_MS = 1000; // start retry after one second
    constexpr static uint32_t RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS = 256000; // retry at least every 256 seconds

    AwsIotConnectivityModule();
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
     * If asynchronous is false this function blocks until it succeeded or failed. Also if asynchronous is false
     * no retries are done to try to reconnected if initial connection could not be established.
     * If asynchronous is true this function returns without being connected and starts a background
     * thread that retries until it was successfully connected or aborts for some reason.
     * This function should be called only once on the object
     * @param privateKey The private key .pem file provided during setup of the AWS
     *                      Iot Thing.
     * @param certificate The certificate .crt.txt file provided during setup
     *                      of the AWS Iot Thing.
     * @param rootCA The Root CA for the certificate
     * @param endpointUrl the endpoint URL normally in the format like
     *                          "[YOUR-THING]-ats.iot.us-west-2.amazonaws.com"
     * @param clientId the id that is used to identify this connection instance
     * @param clientBootstrap pointer to AWS client bootstrap. Note AwsBootstrap is responsible for the client
     * bootstrap lifecycle
     * @param asynchronous if true launch a background thread.
     * @return True if connecting was successful in the synchronous case or if asynchronous true if the establish
     * connection retry thread was successfully started
     */
    bool connect( const std::string &privateKey,
                  const std::string &certificate,
                  const std::string &rootCA,
                  const std::string &endpointUrl,
                  const std::string &clientId,
                  Aws::Crt::Io::ClientBootstrap *clientBootstrap,
                  bool asynchronous = false );

    bool disconnect();

    bool
    isAlive() const override
    {
        return mConnected;
    };

    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection>
    getConnection() const override
    {
        return mConnection;
    }

    /**
     * @brief Increases atomically the memory usage
     * @param bytes number of bytes to reserve
     * @return number of bytes after the increase.
     */
    std::size_t reserveMemoryUsage( std::size_t bytes ) override;

    /**
     * @brief Decreases atomically the memory usage
     * @param bytes number of bytes to release
     * @return number of bytes after the decrease.
     */
    std::size_t releaseMemoryUsage( std::size_t bytes ) override;

    RetryStatus attempt() override;

    void onFinished( RetryStatus code ) override;

    /**
     * @brief create a new channel sharing the connection of this module
     * This call needs to be done before calling connect for all asynchronous subscribe channel
     * @param payloadManager the payload manager used by the new channel,
     * @param maximumIotSDKHeapMemoryBytes the iot sdk heap threshold in bytes after which this channel will stop
     * sending data
     *
     * @return a pointer to the newly created channel. A reference to the newly created channel is also hold inside this
     * module.
     */
    std::shared_ptr<AwsIotChannel> createNewChannel(
        const std::shared_ptr<PayloadManager> &payloadManager,
        std::size_t maximumIotSDKHeapMemoryBytes = AwsIotChannel::MAXIMUM_IOT_SDK_HEAP_MEMORY_BYTES );

private:
    bool createMqttConnection( Aws::Crt::Io::ClientBootstrap *clientBootstrap );
    void setupCallbacks();
    static void renameEventLoopTask();
    bool resetConnection();

    Aws::Crt::ByteCursor mCertificate{ 0, nullptr };
    Aws::Crt::String mEndpointUrl;
    Aws::Crt::ByteCursor mPrivateKey{ 0, nullptr };
    Aws::Crt::ByteCursor mRootCA{ 0, nullptr };
    Aws::Crt::String mClientId;
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mConnection;
    std::unique_ptr<Aws::Iot::MqttClient> mMqttClient;
    RetryThread mRetryThread;

    std::promise<bool> mConnectionCompletedPromise;
    std::promise<void> mConnectionClosedPromise;
    std::atomic<bool> mConnected;
    std::atomic<bool> mConnectionEstablished;

    std::vector<std::shared_ptr<AwsIotChannel>> mChannels;
};
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws
