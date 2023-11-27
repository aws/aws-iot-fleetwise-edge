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

    virtual unsigned getPayloadCountSent() const = 0;
};
} // namespace IoTFleetWise
} // namespace Aws
