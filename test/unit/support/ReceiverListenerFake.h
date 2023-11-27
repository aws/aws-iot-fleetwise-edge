// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IReceiver.h"
#include <cstddef>
#include <cstdint>
#include <gmock/gmock.h>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class ReceiverListenerFake : public IReceiverCallback
{
public:
    std::vector<std::string> mReceivedData;

    void
    onDataReceived( const std::uint8_t *buf, size_t size )
    {
        mReceivedData.push_back( std::string( buf, buf + size ) );
    }
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
