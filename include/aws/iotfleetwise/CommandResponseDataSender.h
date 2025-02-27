// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ISender.h"
#include "command_response.pb.h"
#include <cstddef>
#include <cstdint>
#include <json/json.h>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Sends command responses
 */
class CommandResponseDataSender : public DataSender
{

public:
    CommandResponseDataSender( ISender &sender );

    ~CommandResponseDataSender() override = default;

    bool isAlive() override;

    /**
     * @brief Process command response and prepare data for upload
     */
    void processData( const DataToSend &data, OnDataProcessedCallback callback ) override;

    void processPersistedData( const uint8_t *buf,
                               size_t size,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

private:
    /**
     * @brief member variable used to hold the command response data and minimize heap fragmentation
     */
    Schemas::Commands::CommandResponse mProtoCommandResponseMsg;
    ISender &mMqttSender;
};

} // namespace IoTFleetWise
} // namespace Aws
