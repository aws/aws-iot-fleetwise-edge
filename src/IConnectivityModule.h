// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IConnectivityChannel.h"
#include "PayloadManager.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

class IConnectivityModule
{
public:
    virtual ~IConnectivityModule() = default;

    virtual bool isAlive() const = 0;

    virtual std::shared_ptr<IConnectivityChannel> createNewChannel(
        const std::shared_ptr<PayloadManager> &payloadManager ) = 0;

    virtual bool disconnect() = 0;

    virtual bool connect() = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
