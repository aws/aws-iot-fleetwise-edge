// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>
#include <cstddef>
#include <cstdint>
#include <linux/can.h>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

// clang-format off
/**
 * This class implements interface `ICommandDispatcher`. It's a class for dispatching commands
 * onto CAN, with one CAN request message ID and one CAN response message ID.
 *
 * The command CAN request payload is formed from the null-terminated command ID string, a uint64_t
 * issued timestamp in ms since epoch, a uint64_t relative execution timeout in ms since the issued
 * timestamp, and one actuator argument serialized in network byte order. A relative timeout value
 * of zero means no timeout.
 * Example with command ID "01J3N9DAVV10AA83PZJX561HPS", issued timestamp of 1723134069000, relative
 * timeout of 1000, and actuator datatype int32_t with value 1234567890:
 * |----------------------------------------|----------------------------------------|---------------------------------|---------------------------------|---------------------------|
 * | Payload byte:                          | 0    | 1    | ... | 24   | 25   | 26   | 27   | 28   | ... | 33   | 34   | 35   | 36   | ... | 41   | 42   | 43   | 44   | 45   | 46   |
 * |----------------------------------------|----------------------------------------|---------------------------------|---------------------------------|---------------------------|
 * | Value:                                 | 0x30 | 0x31 | ... | 0x50 | 0x53 | 0x00 | 0x00 | 0x00 | ... | 0x49 | 0x08 | 0x00 | 0x00 | ... | 0x03 | 0xE8 | 0x49 | 0x96 | 0x02 | 0xD2 |
 * |----------------------------------------|----------------------------------------|---------------------------------|---------------------------------|---------------------------|
 * Command ID (null terminated string)-------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Issued timestamp (uint64_t network byte order)-------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Execution timeout (uint64_t network byte order)----------------------------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Request argument (int32_t network byte order)----------------------------------------------------------------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 *
 * The command CAN response payload is formed from the null-terminated command ID string, a 1-byte
 * command status code, a 4-byte uint32_t reason code, and a null-terminated reason description
 * string. The values of the status code correspond with the enum `CommandStatus`
 * Example with command ID "01J3N9DAVV10AA83PZJX561HPS", response status
 * `CommandStatus::EXECUTION_FAILED`, reason code 0x0001ABCD, and reason description "hello":
 * |----------------------------------------|----------------------------------------|------|---------------------------|-----------------------------------------|
 * | Payload byte:                          | 0    | 1    | ... | 24   | 25   | 26   | 27   | 28   | 29   | 30   | 31   | 32   | 33   | 34   | 35   | 36   | 37   |
 * |----------------------------------------|----------------------------------------|------|---------------------------|-----------------------------------------|
 * | Value:                                 | 0x30 | 0x31 | ... | 0x50 | 0x53 | 0x00 | 0x03 | 0x00 | 0x01 | 0xAB | 0xCD | 0x68 | 0x65 | 0x6C | 0x6C | 0x6F | 0x00 |
 * |----------------------------------------|----------------------------------------|------|---------------------------|-----------------------------------------|
 * Command ID (null terminated string)-------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Command status (enum CommandStatus)------------------------------------------------^^^^^^
 * Reason code (uint32_t network byte order)-------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Reason description (null terminated string)---------------------------------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * When a command is dispatched via the `setActuatorValue` interface:
 * 1. The module will store the `commandId` and `notifyStatusCallback`, and then send the CAN
 *    request message. If the execution timeout is greater than zero, a timeout timer is started
 *    that expires at `issuedTimestampMs + executionTimeoutMs`.
 * 2. When a response is received with the matching `commandID` before the timeout timer expires,
 *    the `notifyStatusCallback` will be called with the status, reason code, and reason
 *    description string. If the command status code is `CommandStatus::IN_PROGRESS` the command
 *    will not be considered completed, otherwise it will be considered completed.
 * 3. If no response is received before the timeout timer expires, the `notifyStatusCallback` will
 *    be called with `EXECUTION_TIMEOUT` and the command is considered completed.
 * 4. If a response is received after a command has completed, it is ignored.
 *
 * Note: it is possible to dispatch multiple commands for the same or different actuators
 * concurrently.
 */
// clang-format on
class CanCommandDispatcher : public ICommandDispatcher
{
public:
    struct CommandConfig
    {
        unsigned canRequestId;
        unsigned canResponseId;
        SignalType signalType;
    };

    CanCommandDispatcher( const std::unordered_map<std::string, CommandConfig> &config,
                          std::string canInterfaceName,
                          RawData::BufferManager *rawDataBufferManager );

    ~CanCommandDispatcher() override;

    CanCommandDispatcher( const CanCommandDispatcher & ) = delete;
    CanCommandDispatcher &operator=( const CanCommandDispatcher & ) = delete;
    CanCommandDispatcher( CanCommandDispatcher && ) = delete;
    CanCommandDispatcher &operator=( CanCommandDispatcher && ) = delete;

    /**
     * @brief Initializer command dispatcher with its associated underlying vehicle network / service
     * @return True if successful. False otherwise.
     */
    bool init() override;

    /**
     * @brief set actuator value
     * @param actuatorName Actuator name
     * @param signalValue Signal value
     * @param commandId Command ID
     * @param issuedTimestampMs Timestamp of when the command was issued in the cloud in ms since
     * epoch.
     * @param executionTimeoutMs Relative execution timeout in ms since `issuedTimestampMs`. A value
     * of zero means no timeout.
     * @param notifyStatusCallback Callback to notify command status
     */
    void setActuatorValue( const std::string &actuatorName,
                           const SignalValueWrapper &signalValue,
                           const CommandID &commandId,
                           Timestamp issuedTimestampMs,
                           Timestamp executionTimeoutMs,
                           NotifyCommandStatusCallback notifyStatusCallback ) override;

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
    std::vector<std::string> getActuatorNames() override;

private:
    /**
     * @brief map of supported actuators
     */
    const std::unordered_map<std::string, CommandConfig> &mConfig;
    std::string mCanInterfaceName;
    std::unordered_map<unsigned, const std::string &> mResponseIdToActuatorName;

    struct ExecutionState
    {
        ExecutionState( const std::string &actuatorNameIn,
                        unsigned canResponseIdIn,
                        NotifyCommandStatusCallback notifyStatusCallbackIn,
                        boost::asio::steady_timer timerIn )
            : actuatorName( actuatorNameIn )
            , canResponseId( canResponseIdIn )
            , notifyStatusCallback( std::move( notifyStatusCallbackIn ) )
            , timer( std::move( timerIn ) )
        {
        }
        const std::string &actuatorName;
        unsigned canResponseId;
        NotifyCommandStatusCallback notifyStatusCallback;
        boost::asio::steady_timer timer;
        struct canfd_frame sendCanFrame = {};
    };
    std::unordered_map<CommandID, ExecutionState> mExecutionState;
    std::mutex mExecutionStateMutex;

    boost::asio::io_context mIoContext;
    boost::asio::posix::basic_stream_descriptor<> mIoStream;
    int mCanSocket{ -1 };
    std::thread mThread;
    RawData::BufferManager *mRawDataBufferManager;

    bool setupCan();
    bool setupReception();
    void setupTimeout( boost::asio::steady_timer &timer, std::string commandId, Timestamp timeoutMs );
    void handleCanFrameReception( const boost::system::error_code &error, size_t len );
    void handleTimeout( const boost::system::error_code &error, std::string commandId );
    static bool popString( const struct canfd_frame &canFrame, size_t &index, std::string &str );
    static bool pushString( struct canfd_frame &canFrame, const std::string &str );

    template <typename T>
    static bool
    popNetworkByteOrder( const struct canfd_frame &canFrame, size_t &index, T &value )
    {
        if ( ( index + sizeof( T ) ) > canFrame.len )
        {
            return false;
        }
        value = boost::endian::endian_load<T, sizeof( T ), boost::endian::order::big>( &canFrame.data[index] );
        index += sizeof( T );
        return true;
    }

    template <typename T>
    static bool
    pushNetworkByteOrder( struct canfd_frame &canFrame, const T &value )
    {
        if ( ( canFrame.len + sizeof( T ) ) > CANFD_MAX_DLEN )
        {
            return false;
        }
        boost::endian::endian_store<T, sizeof( T ), boost::endian::order::big>( &canFrame.data[canFrame.len], value );
        canFrame.len = static_cast<uint8_t>( canFrame.len + static_cast<uint8_t>( sizeof( T ) ) );
        return true;
    }

    bool pushArgument( struct canfd_frame &canFrame, const SignalValueWrapper &signalValue );
};

} // namespace IoTFleetWise
} // namespace Aws
