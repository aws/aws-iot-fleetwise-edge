// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CanCommandDispatcher.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "LoggingModule.h"
#include "Thread.h"
#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstring>
#include <exception>
#include <functional>
#include <linux/can/raw.h>
#include <memory>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{

CanCommandDispatcher::CanCommandDispatcher( const std::unordered_map<std::string, CommandConfig> &config,
                                            std::string canInterfaceName,
                                            std::shared_ptr<RawData::BufferManager> rawBufferManager )
    : mConfig{ config }
    , mCanInterfaceName{ std::move( canInterfaceName ) }
    , mIoStream( mIoContext )
    , mRawBufferManager( std::move( rawBufferManager ) )
{
}

CanCommandDispatcher::~CanCommandDispatcher()
{
    mIoContext.stop();
    if ( mThread.joinable() )
    {
        mThread.join();
    }
    close( mCanSocket );
}

std::vector<std::string>
CanCommandDispatcher::getActuatorNames()
{
    std::vector<std::string> actuatorNames;
    for ( const auto &config : mConfig )
    {
        actuatorNames.push_back( config.first );
    }
    return actuatorNames;
}

bool
CanCommandDispatcher::popString( const struct canfd_frame &canFrame, size_t &index, std::string &str )
{
    // coverity[INFINITE_LOOP] False positive, loop can exit
    while ( index < canFrame.len )
    {
        if ( canFrame.data[index] == 0x00 )
        {
            index++;
            return true;
        }
        str += static_cast<char>( canFrame.data[index] );
        index++;
    }
    return false;
}

bool
CanCommandDispatcher::pushString( struct canfd_frame &canFrame, const std::string &str )
{
    if ( ( canFrame.len + str.size() + 1 ) > CANFD_MAX_DLEN )
    {
        return false;
    }
    memcpy( &canFrame.data[canFrame.len], str.data(), str.size() );
    canFrame.len = static_cast<uint8_t>( canFrame.len + static_cast<uint8_t>( str.size() ) );
    canFrame.data[canFrame.len] = 0x00;
    canFrame.len++;
    return true;
}

bool
CanCommandDispatcher::pushArgument( struct canfd_frame &canFrame, const SignalValueWrapper &signalValue )
{
    switch ( signalValue.type )
    {
    case SignalType::BOOLEAN: {
        uint8_t boolValU8 = static_cast<uint8_t>( signalValue.value.boolVal );
        return pushNetworkByteOrder( canFrame, boolValU8 );
    }
    case SignalType::UINT8:
        return pushNetworkByteOrder( canFrame, signalValue.value.uint8Val );
    case SignalType::INT8:
        return pushNetworkByteOrder( canFrame, signalValue.value.int8Val );
    case SignalType::UINT16:
        return pushNetworkByteOrder( canFrame, signalValue.value.uint16Val );
    case SignalType::INT16:
        return pushNetworkByteOrder( canFrame, signalValue.value.int16Val );
    case SignalType::UINT32:
        return pushNetworkByteOrder( canFrame, signalValue.value.uint32Val );
    case SignalType::INT32:
        return pushNetworkByteOrder( canFrame, signalValue.value.int32Val );
    case SignalType::UINT64:
        return pushNetworkByteOrder( canFrame, signalValue.value.uint64Val );
    case SignalType::INT64:
        return pushNetworkByteOrder( canFrame, signalValue.value.int64Val );
    case SignalType::FLOAT:
        return pushNetworkByteOrder( canFrame, signalValue.value.floatVal );
    case SignalType::DOUBLE:
        return pushNetworkByteOrder( canFrame, signalValue.value.doubleVal );
    case SignalType::STRING: {
        auto loanedFrame = mRawBufferManager->borrowFrame( signalValue.value.rawDataVal.signalId,
                                                           signalValue.value.rawDataVal.handle );
        if ( loanedFrame.isNull() )
        {
            return false;
        }
        std::string stringVal;
        stringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
        return pushString( canFrame, stringVal );
    }
    default:
        FWE_LOG_ERROR( "Unsupported datatype " + std::to_string( static_cast<int>( signalValue.type ) ) );
        return false;
    }
}

void
CanCommandDispatcher::handleCanFrameReception( const boost::system::error_code &error, size_t len )
{
    static_cast<void>( len );
    if ( error != boost::system::errc::success )
    {
        FWE_LOG_ERROR( "Error reading from socket: " + error.message() );
        return;
    }
    struct canfd_frame receivedCanFrame;
    size_t bytesTransferred = 0;
    try
    {
        bytesTransferred = mIoStream.read_some( boost::asio::buffer( &receivedCanFrame, sizeof( receivedCanFrame ) ) );
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Error during read: " + std::string( e.what() ) );
        // Continue: setup reception, then return due to bytesTransferred != CANFD_MTU below
    }
    static_cast<void>( setupReception() );
    if ( bytesTransferred != CANFD_MTU )
    {
        return;
    }
    auto actuatorNameIt = mResponseIdToActuatorName.find( receivedCanFrame.can_id );
    if ( actuatorNameIt == mResponseIdToActuatorName.end() )
    {
        return;
    }
    size_t index = 0;
    CommandID receivedCommandId;
    if ( !popString( receivedCanFrame, index, receivedCommandId ) )
    {
        FWE_LOG_ERROR( "Could not pop null-terminated string for command id" );
        return;
    }
    uint8_t statusU8{};
    if ( !popNetworkByteOrder( receivedCanFrame, index, statusU8 ) )
    {
        FWE_LOG_ERROR( "Could not pop status code" );
        return;
    }
    // coverity[autosar_cpp14_a7_2_1_violation] To reduce maintenance effort, cast directly to enum
    CommandStatus status = static_cast<CommandStatus>( statusU8 );
    uint32_t reasonCode{};
    if ( !popNetworkByteOrder( receivedCanFrame, index, reasonCode ) )
    {
        FWE_LOG_ERROR( "Could not pop reason code" );
        return;
    }
    std::string reasonDescription;
    if ( !popString( receivedCanFrame, index, reasonDescription ) )
    {
        FWE_LOG_ERROR( "Could not pop null-terminated string for reason description" );
        return;
    }
    std::lock_guard<std::mutex> lock( mExecutionStateMutex );
    auto executionStateIt = mExecutionState.find( receivedCommandId );
    if ( executionStateIt == mExecutionState.end() )
    {
        FWE_LOG_WARN( "Received response for actuator " + actuatorNameIt->second + " with command id " +
                      receivedCommandId + ", status " + commandStatusToString( status ) + ", reason code " +
                      std::to_string( reasonCode ) + ", reason description " + reasonDescription +
                      ", but command id is not active - possibly timed out" );
        return;
    }
    if ( receivedCanFrame.can_id != executionStateIt->second.canResponseId )
    {
        FWE_LOG_WARN( "Received response for actuator " + executionStateIt->second.actuatorName + " with command id " +
                      receivedCommandId + ", status " + commandStatusToString( status ) + ", reason code " +
                      std::to_string( reasonCode ) + ", reason description " + reasonDescription +
                      ", but with wrong CAN id: " + std::to_string( receivedCanFrame.can_id ) + " vs " +
                      std::to_string( executionStateIt->second.canResponseId ) );
        return;
    }
    FWE_LOG_INFO( "Received response for actuator " + actuatorNameIt->second + " with command id " + receivedCommandId +
                  ", status " + commandStatusToString( status ) + ", reason code " + std::to_string( reasonCode ) +
                  ", reason description " + reasonDescription );
    executionStateIt->second.notifyStatusCallback( status, reasonCode, reasonDescription );
    if ( status != CommandStatus::IN_PROGRESS )
    {
        mExecutionState.erase( receivedCommandId );
    }
}

void
CanCommandDispatcher::handleTimeout( const boost::system::error_code &error, std::string commandId )
{
    if ( error == boost::asio::error::operation_aborted )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( mExecutionStateMutex );
    auto executionStateIt = mExecutionState.find( commandId );
    if ( executionStateIt == mExecutionState.end() )
    {
        FWE_LOG_ERROR( "Could not find execution state for command id " + commandId );
        return;
    }
    FWE_LOG_WARN( "Execution timeout for actuator " + executionStateIt->second.actuatorName + " with command id " +
                  commandId );
    executionStateIt->second.notifyStatusCallback( CommandStatus::EXECUTION_TIMEOUT, REASON_CODE_NO_RESPONSE, "" );
    mExecutionState.erase( commandId );
}

bool
CanCommandDispatcher::setupReception()
{
    try
    {
        mIoStream.async_read_some(
            boost::asio::null_buffers(),
            std::bind(
                &CanCommandDispatcher::handleCanFrameReception, this, std::placeholders::_1, std::placeholders::_2 ) );
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Error starting async read: " + std::string( e.what() ) );
        return false;
    }
    return true;
}

void
CanCommandDispatcher::setupTimeout( boost::asio::steady_timer &timer, std::string commandId, Timestamp timeoutMs )
{
    try
    {
        timer.expires_after( std::chrono::milliseconds( timeoutMs ) );
        timer.async_wait( std::bind( &CanCommandDispatcher::handleTimeout, this, std::placeholders::_1, commandId ) );
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Error starting async timer: " + std::string( e.what() ) );
        return;
    }
}

bool
CanCommandDispatcher::init()
{
    for ( const auto &commandConfig : mConfig )
    {
        mResponseIdToActuatorName.emplace( commandConfig.second.canResponseId, commandConfig.first );
    }
    if ( !setupCan() )
    {
        return false;
    }
    try
    {
        mIoStream.assign( mCanSocket );
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Error setting socket fd: " + std::string( e.what() ) );
        return false;
    }
    if ( !setupReception() )
    {
        return false;
    }

    // Start event loop thread:
    mThread = std::thread( [this]() {
        Thread::setCurrentThreadName( "CanCmdDsp" );
        // Prevent the io context loop from exiting when it runs out of work
        auto workGuard = boost::asio::make_work_guard( mIoContext );
        mIoContext.run();
    } );
    FWE_LOG_INFO( "Successfully initialized CAN command interface on " + mCanInterfaceName );
    return true;
}

void
CanCommandDispatcher::setActuatorValue( const std::string &actuatorName,
                                        const SignalValueWrapper &signalValue,
                                        const CommandID &commandId,
                                        Timestamp issuedTimestampMs,
                                        Timestamp executionTimeoutMs,
                                        NotifyCommandStatusCallback notifyStatusCallback )
{
    auto configIt = mConfig.find( actuatorName );
    if ( configIt == mConfig.end() )
    {
        FWE_LOG_WARN( "Actuator not supported: " + actuatorName + ", command id " + commandId );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_NOT_SUPPORTED, "" );
        return;
    }
    if ( configIt->second.signalType != signalValue.type )
    {
        FWE_LOG_WARN( "Argument type mismatch for " + actuatorName + " and command id " + commandId );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_ARGUMENT_TYPE_MISMATCH, "" );
        return;
    }
    Timestamp remainingTimeoutMs = 0;
    if ( executionTimeoutMs > 0 )
    {
        auto currentTimeMs = ClockHandler::getClock()->systemTimeSinceEpochMs();
        if ( ( issuedTimestampMs + executionTimeoutMs ) <= currentTimeMs )
        {
            FWE_LOG_ERROR( "Command Request with ID " + commandId + " timed out" );
            notifyStatusCallback( CommandStatus::EXECUTION_TIMEOUT, REASON_CODE_TIMED_OUT_BEFORE_DISPATCH, "" );
            return;
        }
        remainingTimeoutMs = issuedTimestampMs + executionTimeoutMs - currentTimeMs;
    }
    std::lock_guard<std::mutex> lock( mExecutionStateMutex );
    auto emplaceResult = mExecutionState.emplace( commandId,
                                                  ExecutionState{ configIt->first,
                                                                  configIt->second.canResponseId,
                                                                  notifyStatusCallback,
                                                                  boost::asio::steady_timer( mIoContext ) } );
    if ( !emplaceResult.second )
    {
        FWE_LOG_WARN( "Ignoring duplicate command id " + commandId + " for actuator " + configIt->first );
        return;
    }
    auto &executionState = emplaceResult.first->second;
    // Send CAN request message:
    executionState.sendCanFrame.can_id = configIt->second.canRequestId;
    if ( ( !pushString( executionState.sendCanFrame, commandId ) ) ||
         ( !pushNetworkByteOrder( executionState.sendCanFrame, issuedTimestampMs ) ) ||
         ( !pushNetworkByteOrder( executionState.sendCanFrame, executionTimeoutMs ) ) ||
         ( !pushArgument( executionState.sendCanFrame, signalValue ) ) )
    {
        FWE_LOG_ERROR( "Error pushing data for " + actuatorName + " and command id " + commandId );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, "" );
        mExecutionState.erase( commandId );
        return;
    }
    FWE_LOG_INFO( "Sending request for actuator " + actuatorName + " and command id " + commandId );
    auto handleSendComplete = [this, commandId]( const boost::system::error_code &error, size_t bytesTransferred ) {
        std::lock_guard<std::mutex> writeLock( mExecutionStateMutex );
        auto executionStateIt = mExecutionState.find( commandId );
        if ( executionStateIt == mExecutionState.end() )
        {
            FWE_LOG_WARN( "Write to socket completed after timeout for command id " + commandId );
            return;
        }
        if ( error != boost::system::errc::success )
        {
            FWE_LOG_ERROR( "Error writing to socket: " + error.message() );
            executionStateIt->second.notifyStatusCallback(
                CommandStatus::EXECUTION_FAILED, REASON_CODE_WRITE_FAILED, "Writing to CAN socket failed" );
            mExecutionState.erase( commandId );
            return;
        }
        if ( bytesTransferred != CANFD_MTU )
        {
            FWE_LOG_ERROR( "Unexpected number of bytes transferred: " + std::to_string( bytesTransferred ) );
            executionStateIt->second.notifyStatusCallback(
                CommandStatus::EXECUTION_FAILED, REASON_CODE_WRITE_FAILED, "Writing to CAN socket failed" );
            mExecutionState.erase( commandId );
            return;
        }
        FWE_LOG_INFO( "Request sent for actuator " + executionStateIt->second.actuatorName + " and command id " +
                      commandId );
    };
    try
    {
        mIoStream.async_write_some(
            boost::asio::buffer( &executionState.sendCanFrame, sizeof( executionState.sendCanFrame ) ),
            handleSendComplete );
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Error starting async write: " + std::string( e.what() ) );
        notifyStatusCallback(
            CommandStatus::EXECUTION_FAILED, REASON_CODE_WRITE_FAILED, "Writing to CAN socket failed" );
        mExecutionState.erase( commandId );
        return;
    }

    if ( remainingTimeoutMs > 0 )
    {
        setupTimeout( executionState.timer, commandId, remainingTimeoutMs );
    }
}

bool
CanCommandDispatcher::setupCan()
{
    // Setup CAN-FD socket:
    mCanSocket = socket( PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW );
    if ( mCanSocket < 0 )
    {
        FWE_LOG_ERROR( "Failed to create socket: " + getErrnoString() );
        return false;
    }
    int canfdOn = 1;
    if ( setsockopt( mCanSocket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfdOn, sizeof( canfdOn ) ) != 0 )
    {
        FWE_LOG_ERROR( "setsockopt CAN_RAW_FD_FRAMES FAILED" );
        close( mCanSocket );
        return false;
    }
    auto interfaceIndex = if_nametoindex( mCanInterfaceName.c_str() );
    if ( interfaceIndex == 0 )
    {
        FWE_LOG_ERROR( "CAN Interface with name " + mCanInterfaceName + " is not accessible" );
        close( mCanSocket );
        return false;
    }
    struct sockaddr_can interfaceAddress = {};
    interfaceAddress.can_family = AF_CAN;
    interfaceAddress.can_ifindex = static_cast<int>( interfaceIndex );
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    if ( bind( mCanSocket, (struct sockaddr *)&interfaceAddress, sizeof( interfaceAddress ) ) < 0 )
    {
        FWE_LOG_ERROR( "Failed to bind socket: " + getErrnoString() );
        close( mCanSocket );
        return false;
    }
    return true;
}

} // namespace IoTFleetWise
} // namespace Aws
