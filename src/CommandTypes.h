// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "ICommandDispatcher.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include <boost/variant.hpp>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

struct ActuatorCommandRequest
{

    /**
     * @brief Unique Command ID generated on the cloud
     */
    CommandID commandID;

    /**
     * @brief Decoder Manifest sync id associated with this command request
     */
    SyncID decoderID;

    /**
     * @brief Signal ID associated with the Command
     */
    SignalID signalID{ 0 };

    /**
     * @brief Contains signal value and type to be set with the Command
     */
    SignalValueWrapper signalValueWrapper;

    /**
     * @brief Timestamp in ms since epoch of when the command was issued in the cloud
     */
    Timestamp issuedTimestampMs{ 0 };

    /**
     * @brief Command execution timeout in ms since `issuedTimestampMs`
     */
    Timestamp executionTimeoutMs{ 0 };
};

enum class LastKnownStateOperation
{
    ACTIVATE = 0,
    DEACTIVATE = 1,
    FETCH_SNAPSHOT = 2,
};

struct LastKnownStateCommandRequest
{
    /**
     * @brief Unique Command ID generated on the cloud
     */
    CommandID commandID;

    /**
     * @brief State template sync id associated with this command request
     */
    SyncID stateTemplateID;

    /**
     * @brief The operation that should be applied to this state template
     */
    LastKnownStateOperation operation;

    /**
     * @brief Make the collection to be stopped after this time.
     *
     * This is only used when the operation is ACTIVATE
     */
    uint32_t deactivateAfterSeconds{ 0 };

    /**
     * @brief Time when this command was received
     */
    TimePoint receivedTime{ 0, 0 };
};

/**
 * Response related to a single command to be sent to the cloud
 */
struct CommandResponse : DataToSend
{
    CommandID id;
    CommandStatus status;
    CommandReasonCode reasonCode;
    CommandReasonDescription reasonDescription;

    CommandResponse( CommandID commandID,
                     CommandStatus commandStatus,
                     CommandReasonCode commandReasonCode,
                     CommandReasonDescription commandReasonDescription )
        : id( std::move( commandID ) )
        , status( std::move( commandStatus ) )
        , reasonCode( commandReasonCode )
        , reasonDescription( std::move( commandReasonDescription ) )
    {
    }

    ~CommandResponse() override = default;

    SenderDataType
    getDataType() const override
    {
        return SenderDataType::COMMAND_RESPONSE;
    }
};

} // namespace IoTFleetWise
} // namespace Aws
