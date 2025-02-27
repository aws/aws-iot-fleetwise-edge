// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include <CommonAPI/CommonAPI.hpp>
#include <functional>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

static inline std::string
commonapiCallStatusToString( CommonAPI::CallStatus callStatus )
{
    switch ( callStatus )
    {
    case CommonAPI::CallStatus::SUCCESS:
        return "SUCCESS";
    case CommonAPI::CallStatus::OUT_OF_MEMORY:
        return "OUT_OF_MEMORY";
    case CommonAPI::CallStatus::NOT_AVAILABLE:
        return "NOT_AVAILABLE";
    case CommonAPI::CallStatus::CONNECTION_FAILED:
        return "CONNECTION_FAILED";
    case CommonAPI::CallStatus::REMOTE_ERROR:
        return "REMOTE_ERROR";
    case CommonAPI::CallStatus::UNKNOWN:
        return "UNKNOWN";
    case CommonAPI::CallStatus::INVALID_VALUE:
        return "INVALID_VALUE";
    case CommonAPI::CallStatus::SUBSCRIPTION_REFUSED:
        return "SUBSCRIPTION_REFUSED";
    case CommonAPI::CallStatus::SERIALIZATION_ERROR:
        return "SERIALIZATION_ERROR";
    default:
        return "Unknown error: " + std::to_string( static_cast<int>( callStatus ) );
    }
}

static inline CommandStatus
commonapiCallStatusToCommandStatus( CommonAPI::CallStatus callStatus )
{
    return ( callStatus == CommonAPI::CallStatus::SUCCESS ) ? CommandStatus::SUCCEEDED
                                                            : CommandStatus::EXECUTION_FAILED;
}

static inline CommandReasonCode
commonapiCallStatusToReasonCode( CommonAPI::CallStatus callStatus )
{
    return static_cast<CommandReasonCode>( callStatus ) + REASON_CODE_OEM_RANGE_START;
}

static inline CommonAPI::Timeout_t
commonapiGetRemainingTimeout( Timestamp issuedTimestampMs, Timestamp executionTimeoutMs )
{
    if ( executionTimeoutMs == 0 )
    {
        // No timeout
        return -1; // Defined in capicxx-core-runtime include/CommonAPI/Types.hpp
    }
    auto currentTimeMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
    if ( ( issuedTimestampMs + executionTimeoutMs ) <= currentTimeMs )
    {
        return 0; // Already timed-out
    }
    return static_cast<CommonAPI::Timeout_t>( issuedTimestampMs + executionTimeoutMs - currentTimeMs );
}

// type for someip method wrapper
using SomeipMethodWrapperType = std::function<void( SignalValueWrapper signalValue,
                                                    const CommandID &commandId,
                                                    Timestamp issuedTimestampMs,
                                                    Timestamp executionTimeoutMs,
                                                    NotifyCommandStatusCallback notifyStatusCallback )>;

/**
 * SomeipMethodInfo is a struct contains the information about method: input value type, method signature
 */
struct SomeipMethodInfo
{
    SignalType signalType;
    SomeipMethodWrapperType methodWrapper;
};

class ISomeipInterfaceWrapper
{
public:
    virtual ~ISomeipInterfaceWrapper() = default;
    virtual bool init() = 0;
    virtual std::shared_ptr<CommonAPI::Proxy> getProxy() const = 0;
    virtual const std::unordered_map<std::string, SomeipMethodInfo> &getSupportedActuatorInfo() const = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
