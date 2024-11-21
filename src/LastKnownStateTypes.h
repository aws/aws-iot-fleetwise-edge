// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "DataSenderTypes.h"
#include "ICommandDispatcher.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include <boost/variant.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

constexpr auto REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_ACTIVATED =
    "State template activated successfully. The auto-stop value has been updated to reflect the latest settings.";
constexpr auto REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_DEACTIVATED =
    "No action taken. The state template you attempted to deactivate was already deactivated.";

enum class LastKnownStateUpdateStrategy
{
    PERIODIC = 0,
    ON_CHANGE = 1,
};

struct LastKnownStateSignalInformation
{
    SignalID signalID;
    SignalType signalType{ SignalType::DOUBLE };
};

struct StateTemplateInformation
{
    SyncID id;
    SyncID decoderManifestID;
    std::vector<LastKnownStateSignalInformation> signals;
    LastKnownStateUpdateStrategy updateStrategy;
    // For periodic update strategy only. Indicates the interval to periodically send the data.
    uint64_t periodMs{ 0 };
};

using StateTemplateList = std::vector<std::shared_ptr<const StateTemplateInformation>>;

struct StateTemplatesDiff
{
    uint64_t version{ 0 };
    StateTemplateList stateTemplatesToAdd;
    std::vector<SyncID> stateTemplatesToRemove;
};

struct StateTemplateCollectedSignals
{
    SyncID stateTemplateSyncId;
    std::vector<CollectedSignal> signals;
};

struct LastKnownStateCollectedData : DataToSend
{
    Timestamp triggerTime;
    std::vector<StateTemplateCollectedSignals> stateTemplateCollectedSignals;

    ~LastKnownStateCollectedData() override = default;

    SenderDataType
    getDataType() const override
    {
        return SenderDataType::LAST_KNOWN_STATE;
    }
};

} // namespace IoTFleetWise
} // namespace Aws
