// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IConnectionTypes.h"
#include "Listener.h"

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Receiver must inherit from IReceiverCallback
 * @see IReceiver
 */
struct IReceiverCallback
{
    virtual ~IReceiverCallback() = default;

    /**
     * @brief called after new data received
     *
     * Be cautious the callback onDataReceived will happen from a different thread and the callee
     * needs to ensure that the data is treated in a thread safe manner when copying it.
     * The function behind onDataReceived must be fast (<1ms) and the pointer buf will get
     * invalid after returning from onDataReceived.
     *
     * @param buf pointer to raw received data that will be at least size long.
     *               The function does not care if the data is a c string, a json or a binary
     *               data stream like proto buf. The pointer is invalid after the function returned
     * @param size number of accessible bytes in buf
     */
    virtual void onDataReceived( const std::uint8_t *buf, size_t size ) = 0;
};

/**
 * @brief This interface will be used by all objects receiving data from the cloud
 *
 * The configuration will done by the bootstrap with the implementing class.
 * To register an IReceiverCallback use the subscribeListener inherited from ThreadListeners.
 *  \code{.cpp}
 *  class ExampleReceiver:IReceiverCallback {
 *    startReceiving(IReceiver &r) {
 *        r.subscribeListener(this);
 *    }
 *    onDataReceived( std::uint8_t *buf, size_t size ) {
 *    // copy buf if needed
 *    }
 *   };
 *  \endcode
 * @see IReceiverCallback
 */
class IReceiver : public ThreadListeners<IReceiverCallback>
{

public:
    ~IReceiver() override = default;
    /**
     * @brief indicates if the connection is established and authenticated
     *
     * The function exists only to provide some status for example for monitoring but most users of
     * of this interface do not need to call it
     * */
    virtual bool isAlive() = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
