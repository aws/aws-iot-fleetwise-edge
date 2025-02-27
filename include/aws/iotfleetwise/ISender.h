// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Struct that specifies the persistence and transmission attributes
 *        regarding the edge to cloud payload
 */
struct CollectionSchemeParams
{
    bool persist{ false };     // specifies if data needs to be persisted in case of connection loss
    bool compression{ false }; // specifies if data needs to be compressed for cloud
    uint32_t priority{ 0 };    // collectionScheme priority specified by the cloud
    uint64_t triggerTime{ 0 }; // timestamp of event ocurred
    uint32_t eventID{ 0 };     // event id
};

/**
 * @brief called after data is sent
 *
 * Be cautious this callback will happen from a different thread and the callee
 * needs to ensure that the data is treated in a thread safe manner when copying it.
 *
 * @param result whether the data was successfully sent or not
 */
using OnDataSentCallback = std::function<void( ConnectivityError result )>;

/**
 * @brief called after the mqtt client is connected
 */
using OnConnectionEstablishedCallback = std::function<void()>;

enum class QoS
{
    AT_MOST_ONCE = 0,
    AT_LEAST_ONCE = 1,
};

/**
 * @brief This interface will be used by all objects sending data to the cloud
 *
 * The configuration will done by the bootstrap with the implementing class.
 */
class ISender
{

public:
    virtual ~ISender() = default;

    /**
     * @brief indicates if the connection is established and authenticated
     *
     * The function exists only to provide some status for example for monitoring but most users of
     * of this interface do not need to call it
     * */
    virtual bool isAlive() = 0;

    /**
     * @brief get the maximum bytes that can be sent
     *
     * @return number of bytes accepted by the send function
     * */
    virtual size_t getMaxSendSize() const = 0;

    /**
     * @brief called to send data to the cloud
     *
     * The function will return fast and does not expect the parameters to outlive the
     * function call. It can be called from any thread because the function will if needed
     * copy the buffer.
     *
     * @param topic the topic to send the data to.
     * @param buf pointer to raw data to send that needs to be at least size long.
     *               The function does not care if the data is a c string, a json or a binary
     *               data stream like proto buf. The data behind buf will not be modified.
     *               The data in this buffer is associated with one collectionScheme.
     * @param size number of accessible bytes in buf. If bigger than getMaxSendSize() this function
     *              will return an error and nothing will be sent.
     * @param callback callback that will be called when the operation completes (successfully or not).
     *                 IMPORTANT: The callback can be called by the same thread before sendBuffer even returns
     *                 or a separate thread, depending on whether the results are known synchronously or asynchronously.
     * @param qos the QoS level for the publish messages
     */
    virtual void sendBuffer( const std::string &topic,
                             const std::uint8_t *buf,
                             size_t size,
                             OnDataSentCallback callback,
                             QoS qos = QoS::AT_LEAST_ONCE ) = 0;

    virtual unsigned getPayloadCountSent() const = 0;

    virtual const TopicConfig &getTopicConfig() const = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
