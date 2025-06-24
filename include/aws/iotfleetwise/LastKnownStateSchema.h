// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/LastKnownStateIngestion.h"
#include "aws/iotfleetwise/Listener.h"
#include <functional>
#include <memory>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This class handles received state templates
 */
class LastKnownStateSchema
{
public:
    /**
     * @brief Callback function used to notify when a new state template arrives from the Cloud.
     *
     * @param stateTemplates
     */
    using OnLastKnownStateReceivedCallback =
        std::function<void( const std::shared_ptr<LastKnownStateIngestion> lastKnownStateIngestion )>;

    /**
     * @param receiverLastKnownState Receiver for a state templates.proto message on a LastKnownState
     * topic
     */
    LastKnownStateSchema( IReceiver &receiverLastKnownState );

    ~LastKnownStateSchema() = default;

    LastKnownStateSchema( const LastKnownStateSchema & ) = delete;
    LastKnownStateSchema &operator=( const LastKnownStateSchema & ) = delete;
    LastKnownStateSchema( LastKnownStateSchema && ) = delete;
    LastKnownStateSchema &operator=( LastKnownStateSchema && ) = delete;

    void
    subscribeToLastKnownStateReceived( OnLastKnownStateReceivedCallback callback )
    {
        mLastKnownStateListeners.subscribe( std::move( callback ) );
    }

private:
    /**
     * @brief Callback that should be called whenever a new message with state templates is received
     * from the Cloud.
     * @param receivedMessage struct containing message data and metadata
     */
    void onLastKnownStateReceived( const ReceivedConnectivityMessage &receivedMessage );

    ThreadSafeListeners<OnLastKnownStateReceivedCallback> mLastKnownStateListeners;
};

} // namespace IoTFleetWise
} // namespace Aws
