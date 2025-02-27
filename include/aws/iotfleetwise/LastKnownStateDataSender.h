// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "last_known_state_data.pb.h"
#include <cstddef>
#include <cstdint>
#include <istream>
#include <json/json.h>

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
    LastKnownStateDataSender( ISender &sender, unsigned maxMessagesPerPayload );

    ~LastKnownStateDataSender() override = default;

    bool isAlive() override;

    /**
     * @brief Process last known state and prepare data for upload
     */
    void processData( const DataToSend &data, OnDataProcessedCallback callback ) override;

    void processPersistedData( const uint8_t *buf,
                               size_t size,
                               const Json::Value &metadata,
                               OnPersistedDataProcessedCallback callback ) override;

private:
    /**
     * @brief member variable used to hold the protobuf data and minimize heap fragmentation
     */
    Schemas::LastKnownState::LastKnownStateData mProtoMsg;
    ISender &mMqttSender;

    unsigned mMaxMessagesPerPayload{ 0 }; // max number of messages that can be sent to cloud at one time
    unsigned mMessageCount{ 0 };          // current number of messages in the payload

    void resetProto( Timestamp triggerTime );

    void sendProto( std::stringstream &logMessageIds, const Aws::IoTFleetWise::OnDataProcessedCallback &callback );
};

} // namespace IoTFleetWise
} // namespace Aws
