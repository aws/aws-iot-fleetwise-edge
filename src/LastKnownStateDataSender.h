// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataSenderTypes.h"
#include "ISender.h"
#include "TimeTypes.h"
#include "last_known_state_data.pb.h"
#include <istream>
#include <json/json.h>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Sends LastKnownState data
 */
class LastKnownStateDataSender : public DataSender
{

public:
    LastKnownStateDataSender( std::shared_ptr<ISender> lastKnownStateSender, unsigned maxMessagesPerPayload );

    ~LastKnownStateDataSender() override = default;

    /**
     * @brief Process last known state and prepare data for upload
     */
    void processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback ) override;

    void processPersistedData( std::istream &data,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

private:
    /**
     * @brief member variable used to hold the protobuf data and minimize heap fragmentation
     */
    Schemas::LastKnownState::LastKnownStateData mProtoMsg;
    std::shared_ptr<ISender> mLastKnownStateSender;

    unsigned mMaxMessagesPerPayload{ 0 }; // max number of messages that can be sent to cloud at one time
    unsigned mMessageCount{ 0 };          // current number of messages in the payload

    void resetProto( Timestamp triggerTime );

    void sendProto( std::stringstream &logMessageIds, const Aws::IoTFleetWise::OnDataProcessedCallback &callback );
};

} // namespace IoTFleetWise
} // namespace Aws
