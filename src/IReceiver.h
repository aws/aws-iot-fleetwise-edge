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

struct ReceivedChannelMessage
{
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
     * Key/value pairs that were received together with the data. It might be empty.
     */
    const std::unordered_map<std::string, std::string> &properties;

    /*
     * Absolute MQTT message expiry time since epoch from a monotonic clock.
     */
    Timestamp messageExpiryMonotonicTimeSinceEpochMs{ 0 };
};

/**
 * @brief called after new data received
 *
 * Be cautious the callback onDataReceived will happen from a different thread and the callee
 * needs to ensure that the data is treated in a thread safe manner when copying it.
 * The function behind onDataReceived must be fast (<1ms) and the pointer buf will get
 * invalid after returning from the callback.
 *
 * @param receivedChannelMessage struct containing message data and metadata
 */
using OnDataReceivedCallback = std::function<void( const ReceivedChannelMessage &receivedChannelMessage )>;

// Define some common property names to make it easier for subscribers to extract the properties they
// are interested in when the callback is called.
constexpr auto PROPERTY_NAME_CORRELATION_DATA = "correlation-data";

/**
 * @brief This interface will be used by all objects receiving data from the cloud
 *
 * The configuration will done by the bootstrap with the implementing class.
 * To register an IReceiverCallback use the subscribeToDataReceived method.
 *  \code{.cpp}
 *  class ExampleReceiver:IReceiverCallback {
 *    startReceiving(IReceiver &r) {
 *        r.subscribeToDataReceived(this);
 *    }
 *    onDataReceived( std::uint8_t *buf, size_t size ) {
 *    // copy buf if needed
 *    }
 *   };
 *  \endcode
 * @see IReceiverCallback
 */
class IReceiver
{

public:
    ~IReceiver() = default;
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
