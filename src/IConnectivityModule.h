// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IReceiver.h"
#include "ISender.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

enum class QoS
{
    AT_MOST_ONCE = 0,
    AT_LEAST_ONCE = 1,
};

/**
 * @brief called after the mqtt client is connected
 */
using OnConnectionEstablishedCallback = std::function<void()>;

class IConnectivityModule
{
public:
    virtual ~IConnectivityModule() = default;

    virtual bool isAlive() const = 0;

    /**
     * @brief create a new sender sharing the connection of this module
     *
     * @param topicName the topic which this sender should publish to
     * @param publishQoS the QoS level for the publish messages
     *
     * @return a pointer to the newly created sender. A reference to the newly created sender is also hold inside this
     * module.
     */
    virtual std::shared_ptr<ISender> createSender( const std::string &topicName,
                                                   QoS publishQoS = QoS::AT_MOST_ONCE ) = 0;

    /**
     * @brief create a new receiver sharing the connection of this module
     *
     * @param topicName the topic which this receiver should subscribe to
     *
     * @return a pointer to the newly created receiver. A reference to the newly created receiver is also hold inside
     * this module.
     */
    virtual std::shared_ptr<IReceiver> createReceiver( const std::string &topicName ) = 0;

    virtual bool disconnect() = 0;

    virtual bool connect() = 0;

    /**
     * @brief Subscribe to the event that is triggered when the connection is established
     *
     * @param callback The callback to be called when the connection is established.
     */
    virtual void subscribeToConnectionEstablished( OnConnectionEstablishedCallback callback ) = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
