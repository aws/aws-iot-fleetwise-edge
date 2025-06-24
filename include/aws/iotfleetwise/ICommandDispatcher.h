// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using CommandID = std::string;

// 1:1 mapping with values from Protobuf enum Schemas::Commands::Status
enum class CommandStatus
{
    SUCCEEDED = 1,
    EXECUTION_TIMEOUT = 2,
    EXECUTION_FAILED = 4,
    IN_PROGRESS = 10,
};

static inline std::string
commandStatusToString( CommandStatus status )
{
    switch ( status )
    {
    case CommandStatus::SUCCEEDED:
        return "SUCCEEDED";
    case CommandStatus::EXECUTION_TIMEOUT:
        return "EXECUTION_TIMEOUT";
    case CommandStatus::EXECUTION_FAILED:
        return "EXECUTION_FAILED";
    case CommandStatus::IN_PROGRESS:
        return "IN_PROGRESS";
    }
    return "UNKNOWN " + std::to_string( static_cast<int>( status ) );
}

using CommandReasonCode = uint32_t;
// clang-format off
static constexpr CommandReasonCode REASON_CODE_UNSPECIFIED                             = 0x00000000;
// coverity[autosar_cpp14_a0_1_1_violation] variable is intentionally not used
static constexpr CommandReasonCode REASON_CODE_IOTFLEETWISE_RANGE_START                = 0x00000001;
static constexpr CommandReasonCode REASON_CODE_PRECONDITION_FAILED                     = 0x00000001;
static constexpr CommandReasonCode REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC            = 0x00000002;
static constexpr CommandReasonCode REASON_CODE_NO_DECODING_RULES_FOUND                 = 0x00000003;
static constexpr CommandReasonCode REASON_CODE_COMMAND_REQUEST_PARSING_FAILED          = 0x00000004;
static constexpr CommandReasonCode REASON_CODE_NO_COMMAND_DISPATCHER_FOUND             = 0x00000005;
static constexpr CommandReasonCode REASON_CODE_STATE_TEMPLATE_OUT_OF_SYNC              = 0x00000006;
static constexpr CommandReasonCode REASON_CODE_ARGUMENT_TYPE_MISMATCH                  = 0x00000007;
static constexpr CommandReasonCode REASON_CODE_NOT_SUPPORTED                           = 0x00000008;
// coverity[autosar_cpp14_a0_1_1_violation] variable not used yet
static constexpr CommandReasonCode REASON_CODE_BUSY                                    = 0x00000009;
static constexpr CommandReasonCode REASON_CODE_REJECTED                                = 0x0000000A;
// coverity[autosar_cpp14_a0_1_1_violation] variable not used yet
static constexpr CommandReasonCode REASON_CODE_ACCESS_DENIED                           = 0x0000000B;
static constexpr CommandReasonCode REASON_CODE_ARGUMENT_OUT_OF_RANGE                   = 0x0000000C;
static constexpr CommandReasonCode REASON_CODE_INTERNAL_ERROR                          = 0x0000000D;
static constexpr CommandReasonCode REASON_CODE_UNAVAILABLE                             = 0x0000000E;
static constexpr CommandReasonCode REASON_CODE_WRITE_FAILED                            = 0x0000000F;
static constexpr CommandReasonCode REASON_CODE_STATE_TEMPLATE_ALREADY_ACTIVATED        = 0x00000010;
static constexpr CommandReasonCode REASON_CODE_STATE_TEMPLATE_ALREADY_DEACTIVATED      = 0x00000011;
static constexpr CommandReasonCode REASON_CODE_TIMED_OUT_BEFORE_DISPATCH               = 0x00000012;
static constexpr CommandReasonCode REASON_CODE_NO_RESPONSE                             = 0x00000013;
// coverity[autosar_cpp14_a0_1_1_violation] variable is intentionally not used
static constexpr CommandReasonCode REASON_CODE_IOTFLEETWISE_RANGE_END                  = 0x0000FFFF;
// coverity[autosar_cpp14_a0_1_1_violation] variable is intentionally not used
static constexpr CommandReasonCode REASON_CODE_OEM_RANGE_START                         = 0x00010000;
// coverity[autosar_cpp14_a0_1_1_violation] variable is intentionally not used
static constexpr CommandReasonCode REASON_CODE_OEM_RANGE_END                           = 0x0001FFFF;
// clang-format on
using CommandReasonDescription = std::string;

/**
 * This callback interface is passed down to the command dispatcher as a means to provide the status
 * of a command execution. The callback is thread-safe and can be called on the command dispatcher's
 * thread. The callback can be called multiple times with the status IN_PROGRESS, and/or
 * once with another terminal status value, such as SUCCESSFUL or EXECUTION_FAILED.
 * @param status The command status
 * @param reasonCode Reason code for the status
 * @param reasonDescription Reason description
 */
using NotifyCommandStatusCallback = std::function<void(
    CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription )>;

/**
 * This class is the interface for Command Dispatcher. As Command Dispatcher instances are vehicle
 * network / service dependent, it inherit from this class to provide an unified interface for
 * application to dispatch commands.
 */
class ICommandDispatcher
{
public:
    virtual ~ICommandDispatcher() = default;

    /**
     * @brief Initializer command dispatcher with its associated underlying vehicle network / service
     * @return True if successful. False otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief set actuator value
     * @param actuatorName Actuator name
     * @param signalValue Signal value
     * @param commandId Command ID
     * @param issuedTimestampMs Timestamp of when the command was issued in the cloud in ms since
     * epoch. Note: it is possible that commands are received in a different the order at edge to
     * the order they were issued in the cloud. Depending on the application, this parameter can be
     * used to drop outdated commands or buffer and sort them into the issuing order.
     * @param executionTimeoutMs Relative execution timeout in ms since `issuedTimestampMs`. A value
     * of zero means no timeout.
     * @param notifyStatusCallback Callback to notify command status
     */
    virtual void setActuatorValue( const std::string &actuatorName,
                                   const SignalValueWrapper &signalValue,
                                   const CommandID &commandId,
                                   Timestamp issuedTimestampMs,
                                   Timestamp executionTimeoutMs,
                                   NotifyCommandStatusCallback notifyStatusCallback ) = 0;

    /**
     * @brief Gets the actuator names supported by the command dispatcher
     * @todo The decoder manifest doesn't yet have an indication of whether a signal is
     * READ/WRITE/READ_WRITE. Until it does this interface is needed to get the names of the
     * actuators supported by the command dispatcher, so that for string signals, buffers can be
     * pre-allocated in the RawDataManager by the CollectionSchemeManager when a new decoder
     * manifest arrives. When the READ/WRITE/READ_WRITE usage of a signal is available this
     * interface can be removed.
     * @return List of actuator names
     */
    virtual std::vector<std::string> getActuatorNames() = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
