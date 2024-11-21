// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CommandTypes.h"
#include "DataSenderTypes.h"
#include "IReceiver.h"
#include "Listener.h"
#include "RawDataManager.h"
#include <cstddef>
#include <functional>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Setting a CommandRequest proto byte size limit for file received from Cloud
 */
constexpr size_t COMMAND_REQUEST_BYTE_SIZE_LIMIT = 128000000;

/**
 * @brief This class handles received and sent Command messages
 */
class CommandSchema
{
public:
    /**
     * @brief Callback function used to notify when a new ActuatorCommandRequest arrives from the Cloud.
     *
     * @param commandRequest
     */
    using OnActuatorCommandRequestReceivedCallback =
        std::function<void( const ActuatorCommandRequest &commandRequest )>;

    /**
     * @brief Callback function used to notify when a new LastKnownStateCommandRequest arrives from the Cloud.
     *
     * @param commandRequest
     */
    using OnLastKnownStateCommandRequestReceivedCallback =
        std::function<void( const LastKnownStateCommandRequest &commandRequest )>;

    /**
     * @param receiverCommandRequest Receiver for a command_request.proto message on a CommandRequest topic
     * @param commandResponses queue to send command responses in case of failure when processing a received command
     * @param rawBufferManager Raw data buffer manager for storing string signal values
     */
    CommandSchema( std::shared_ptr<IReceiver> receiverCommandRequest,
                   std::shared_ptr<DataSenderQueue> commandResponses,
                   std::shared_ptr<RawData::BufferManager> rawBufferManager );

    ~CommandSchema() = default;

    CommandSchema( const CommandSchema & ) = delete;
    CommandSchema &operator=( const CommandSchema & ) = delete;
    CommandSchema( CommandSchema && ) = delete;
    CommandSchema &operator=( CommandSchema && ) = delete;

    void
    subscribeToActuatorCommandRequestReceived( OnActuatorCommandRequestReceivedCallback callback )
    {
        mActuatorCommandRequestListeners.subscribe( callback );
    }

    void
    subscribeToLastKnownStateCommandRequestReceived( OnLastKnownStateCommandRequestReceivedCallback callback )
    {
        mLastKnownStateCommandRequestListeners.subscribe( callback );
    }

    /**
     * @brief Callback called when receiving a new message confirming a command response was rejected.
     * @param receivedMessage struct containing message data and metadata
     */
    static void onRejectedCommandResponseReceived( const ReceivedConnectivityMessage &receivedMessage );

private:
    /**
     * @brief Callback that should be called whenever a new message with a CommandRequest is received from the Cloud.
     * @param receivedMessage struct containing message data and metadata
     */
    void onCommandRequestReceived( const ReceivedConnectivityMessage &receivedMessage );

    ThreadSafeListeners<OnActuatorCommandRequestReceivedCallback> mActuatorCommandRequestListeners;
    ThreadSafeListeners<OnLastKnownStateCommandRequestReceivedCallback> mLastKnownStateCommandRequestListeners;
    std::shared_ptr<DataSenderQueue> mCommandResponses;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<RawData::BufferManager> mRawBufferManager;
};

} // namespace IoTFleetWise
} // namespace Aws
