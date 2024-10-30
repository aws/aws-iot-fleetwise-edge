// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IConnectionTypes.h"
#include "Listener.h"
#include "TimeTypes.h"
#include <functional>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

struct ReceivedConnectivityMessage
{
public:
    ReceivedConnectivityMessage( const std::uint8_t *bufIn,
                                 size_t sizeIn,
                                 Timestamp receivedMonotonicTimeMsIn,
                                 std::string mqttTopicIn )
        : buf( bufIn )
        , size( sizeIn )
        , receivedMonotonicTimeMs( receivedMonotonicTimeMsIn )
        , mqttTopic( std::move( mqttTopicIn ) )
    {
    }
    /*
     * Pointer to raw received data that will be at least size long.
     * The function does not care if the data is a c string, a json or a binary
     * data stream like proto buf. The pointer is invalid after the function returned
     */
    const std::uint8_t *buf;

    /*
     * Number of accessible bytes in buf
     */
    size_t size{ 0 };

    /*
     * Time when this message was received. The monotonic clock is used (which is not necessarily a Unix timestamp)
     */
    Timestamp receivedMonotonicTimeMs{ 0 };

    /*
     * MQTT topic name
     */
    std::string mqttTopic;
};

/**
 * @brief called after new data received
 *
 * Be cautious the callback onDataReceived will happen from a different thread and the callee
 * needs to ensure that the data is treated in a thread safe manner when copying it.
 * The function behind onDataReceived must be fast (<1ms) and the pointer buf will get
 * invalid after returning from the callback.
 *
 * @param receivedMessage struct containing message data and metadata
 */
using OnDataReceivedCallback = std::function<void( const ReceivedConnectivityMessage &receivedMessage )>;

/**
 * @brief This interface will be used by all objects receiving data from the cloud
 */
class IReceiver
{

public:
    virtual ~IReceiver() = default;

    /**
     * @brief indicates if the connection is established and authenticated
     *
     * The function exists only to provide some status for example for monitoring but most users of
     * of this interface do not need to call it
     * */
    virtual bool isAlive() = 0;

    /**
     * @brief Register a callback to be called after new data received
     * @param callback The function that will be called each time new data is received
     */
    virtual void subscribeToDataReceived( OnDataReceivedCallback callback ) = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
