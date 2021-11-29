/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

// Includes
#include "ClockHandler.h"
#include "IActiveDecoderDictionaryListener.h"
#include "INetworkChannelConsumer.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "businterfaces/INetworkChannelBridge.h"
#include <atomic>
#include <map>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::Platform;

/**
 * @brief The binder is the entity responsible for :
 * 1- creating all consumers
 * 2- handing the circular buffers of the channels to the consumers in a thread safe way.
 * 3- Manages the life cycle of the consumer thread
 * 4- receives interrupts when the channels are disconnected and reflects that to the consumer state.
 */
class NetworkChannelBinder : public NetworkChannelBridgeListener, public IActiveDecoderDictionaryListener
{
public:
    NetworkChannelBinder();
    virtual ~NetworkChannelBinder();
    /**
     * @brief Adds a Network Channel.
     * @param channel Pointer to the network channel.
     * @return True if the channel has been added.
     */
    bool addNetworkChannel( NetworkChannelPtr channel );
    /**
     * @brief removes a Network Channel
     * @param id Network Channel ID.
     * @return True if the channel has been removed.
     */
    bool removeNetworkChannel( const NetworkChannelID &id );
    /**
     * @brief Binds a Network Channel to consumer
     * @param id Network Channel ID.
     * @param consumer Pointer to the consumer
     * @return True if the circular buffer of the channel is handed over to the consumer.
     * Starts the consumer worker.
     */
    bool bindConsumerToNetworkChannel( NetworkChannelConsumerPtr consumer, const NetworkChannelID &id );
    /**
     * @brief unBinds a Network Channel from the consumer.
     * @param id Network Channel ID.
     * @return True of the consumer worker is stopped.
     */
    bool unBindConsumerFromNetworkChannel( const NetworkChannelID &id );

    /**
     * @brief Reports runtime state of the binder.
     * @return True if alive. False otherwise.
     */
    bool isAlive();

    /**
     * @brief Connect the Binder by starting its worker thread.
     * @return True if successful. False otherwise.
     */
    bool connect();

    /**
     * @brief disconnects all network channels and corresponding consumers. Then stop the worker
     * thread
     * @return True if successful. False otherwise.
     */
    bool disconnect();

    /**
     * @brief From IActiveDecoderDictionaryListener. Effectively listens to the CollectionScheme Management
     * module and propagate that gracefully to the channels and consumers.
     */
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     NetworkChannelProtocol networkProtocol ) override;

    typedef std::map<NetworkChannelID, NetworkChannelConsumerPtr> ChannelsToConsumers;
    typedef std::map<NetworkChannelID, NetworkChannelPtr> IdsToChannels;
    typedef std::map<NetworkChannelID, NetworkChannelState> IdsToStates;

private:
    // atomic state of the thread. If true, we should stop
    bool shouldStop() const;

    // Main work function
    static void doWork( void *data );

    /**
     * @brief starts the binder worker. Worker is ready to bind channels to consumers.
     * @return True of the binder worker is running.
     */
    bool start();

    /**
     * @brief stops the binder worker.
     * @return True of the binder worker is stopped.
     */
    bool stop();

    // Listener Callbacks
    void onNetworkChannelConnected( const NetworkChannelID &id ) override;
    void onNetworkChannelDisconnected( const NetworkChannelID &id ) override;
    bool reConnectConsumer( const NetworkChannelID &id );
    bool disconnectConsumer( const NetworkChannelID &id );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::recursive_mutex mThreadMutex;
    mutable std::recursive_mutex mChannelUpdatesMutex;
    mutable std::mutex mChannelsMutex;
    mutable std::mutex mConsumersMutex;
    IdsToStates mChannelStates;
    Platform::Signal mWait;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    ChannelsToConsumers mChannelsToConsumers;
    IdsToChannels mIdsToChannels;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
