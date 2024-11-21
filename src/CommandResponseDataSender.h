// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataSenderTypes.h"
#include "ISender.h"
#include "command_response.pb.h"
#include <istream>
#include <json/json.h>
#include <memory>

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
    CommandResponseDataSender( std::shared_ptr<ISender> commandResponseSender );

    ~CommandResponseDataSender() override = default;

    /**
     * @brief Process command response and prepare data for upload
     */
    void processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback ) override;

    void processPersistedData( std::istream &data,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

private:
    /**
     * @brief member variable used to hold the command response data and minimize heap fragmentation
     */
    Schemas::Commands::CommandResponse mProtoCommandResponseMsg;
    std::shared_ptr<ISender> mCommandResponseSender;
};

} // namespace IoTFleetWise
} // namespace Aws
