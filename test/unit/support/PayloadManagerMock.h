// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/PayloadManager.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <json/json.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class PayloadManagerMock : public PayloadManager
{
public:
    PayloadManagerMock()
        : PayloadManager( nullptr ){};

    MOCK_METHOD( bool, storeData, ( const std::uint8_t *buf, size_t size, const Json::Value &metadata ), ( override ) );

    MOCK_METHOD( ErrorCode, retrievePayloadMetadata, ( Json::Value & files ), ( override ) );

    MOCK_METHOD( ErrorCode,
                 retrievePayload,
                 ( uint8_t * buf, size_t size, const std::string &filename ),
                 ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
