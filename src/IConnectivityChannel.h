// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IReceiver.h"
#include "ISender.h"
#include "PayloadManager.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

class IConnectivityChannel : public ISender, public IReceiver
{
public:
    ~IConnectivityChannel() override = default;

    /**
     * @brief the topic must be set always before using any functionality of this class
     * @param topicNameRef MQTT topic that will be used for sending or receiving data
     *                      if subscribe was called
     * @param subscribeAsynchronously if true the channel will be subscribed to the topic asynchronously so that the
     * channel can receive data
     *
     */
    virtual void setTopic( const std::string &topicNameRef, bool subscribeAsynchronously = false ) = 0;

    virtual unsigned getPayloadCountSent() const = 0;
};
} // namespace IoTFleetWise
} // namespace Aws
