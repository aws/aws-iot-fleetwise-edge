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

// Includes
#include "NetworkChannelBinder.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

NetworkChannelBinder::NetworkChannelBinder()
{
}

NetworkChannelBinder::~NetworkChannelBinder()
{
    mChannelsToConsumers.clear();
    mIdsToChannels.clear();
    mChannelStates.clear();
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
NetworkChannelBinder::addNetworkChannel( NetworkChannelPtr channel )
{
    // Check if the channel is valid
    if ( channel.get() == nullptr || channel->getChannelID() == INVALID_CHANNEL_ID )
    {
        return false;
    }
    // Connect the channel
    // Insert the Channel if it's not already inserted
    {
        std::lock_guard<std::mutex> lock( mChannelsMutex );
        auto channelIterator = mIdsToChannels.emplace( channel->getChannelID(), channel );
        if ( !channelIterator.second )
        {
            return false;
        }
    }
    // Register Self for Connect and Disconnect Callbacks and connect the channel
    return channel->connect() && channel->subscribeListener( this );
}

bool
NetworkChannelBinder::removeNetworkChannel( const NetworkChannelID &id )
{
    // Check if the channel is valid
    if ( id == INVALID_CHANNEL_ID )
    {
        return false;
    }
    auto backupChannel = NetworkChannelPtr();
    // Lookup the Channel in the internal list
    {
        std::lock_guard<std::mutex> lock( mChannelsMutex );
        auto channelIterator = mIdsToChannels.find( id );
        // Something went wrong... removing a channel that does not exist
        if ( channelIterator == mIdsToChannels.end() )
        {
            return false;
        }
        backupChannel = channelIterator->second;
        mIdsToChannels.erase( channelIterator );
    }
    // Deregister this thread from any of the callbacks of this channel and disconnect the channel
    if ( backupChannel.get() != nullptr )
    {
        return backupChannel->unSubscribeListener( this ) && backupChannel->disconnect();
    }
    else
    {
        return false;
    }
}

bool
NetworkChannelBinder::bindConsumerToNetworkChannel( NetworkChannelConsumerPtr consumer, const NetworkChannelID &id )
{
    // Check if the channelID and the consumer are valid
    if ( consumer.get() == nullptr || id == INVALID_CHANNEL_ID )
    {
        return false;
    }
    // First lookup the channel and check if it's registered
    auto channelIterator = mIdsToChannels.find( id );
    if ( channelIterator == mIdsToChannels.end() )
    {
        return false;
    }
    // Insert the consumer/ID pair
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mChannelsToConsumers.emplace( id, consumer );

        if ( !consumerIterator.second )
        {
            return false;
        }
    }
    // Propagate the Channel metadata
    consumer->setChannelMetadata( std::make_tuple( channelIterator->second->getChannelType(),
                                                   channelIterator->second->getChannelProtocol(),
                                                   channelIterator->second->getChannelIfName() ) );
    // Assign the circular buffer of the channel to the consumer
    consumer->setInputBuffer( channelIterator->second->getBuffer() );
    // Start the consumer worker
    return consumer->connect();
}

bool
NetworkChannelBinder::unBindConsumerFromNetworkChannel( const NetworkChannelID &id )
{
    // Check if the channel ID is valid
    if ( id == INVALID_CHANNEL_ID )
    {
        return false;
    }

    auto backupConsumer = NetworkChannelConsumerPtr();
    // Lookup the consumer registered to this channel
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mChannelsToConsumers.find( id );
        // Something went wrong... No consumer is registered for this channel
        if ( consumerIterator == mChannelsToConsumers.end() )
        {
            return false;
        }
        backupConsumer = consumerIterator->second;
        mChannelsToConsumers.erase( consumerIterator );
    }
    // Disconnect the consumer.
    if ( backupConsumer.get() != nullptr )
    {
        return backupConsumer->disconnect();
    }
    else
    {
        return false;
    }
}

bool
NetworkChannelBinder::disconnectConsumer( const NetworkChannelID &id )
{

    auto backupConsumer = NetworkChannelConsumerPtr();
    // Lookup the consumer registered to this channel
    // release the Mutex after this context to allow addition of consumers.
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mChannelsToConsumers.find( id );
        // Something went wrong... No consumer is registered for this channel
        if ( consumerIterator == mChannelsToConsumers.end() )
        {
            return false;
        }
        backupConsumer = consumerIterator->second;
    }
    // Disconnect the consumer.
    if ( backupConsumer.get() != nullptr )
    {
        return backupConsumer->disconnect();
    }
    else
    {
        return false;
    }
}

bool
NetworkChannelBinder::reConnectConsumer( const NetworkChannelID &id )
{

    auto backupConsumer = NetworkChannelConsumerPtr();
    // Lookup the consumer registered to this channel
    // release the Mutex after this context to allow addition of consumers.
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mChannelsToConsumers.find( id );
        // Something went wrong... No consumer is registered for this channel
        if ( consumerIterator == mChannelsToConsumers.end() )
        {
            return false;
        }
        backupConsumer = consumerIterator->second;
    }
    // reConnect the consumer. Make sure that the the consumer is not alive already
    if ( backupConsumer.get() != nullptr && !backupConsumer->isAlive() )
    {
        return backupConsumer->connect();
    }
    else
    {
        return false;
    }
}

bool
NetworkChannelBinder::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "NetworkChannelBinder::start", " Binder Thread failed to start " );
    }
    else
    {
        mLogger.trace( "NetworkChannelBinder::start", " Binder Thread started " );
        mThread.setThreadName( "fwDIBinder" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
NetworkChannelBinder::stop()
{
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
NetworkChannelBinder::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
NetworkChannelBinder::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

void
NetworkChannelBinder::doWork( void *data )
{

    NetworkChannelBinder *binder = static_cast<NetworkChannelBinder *>( data );
    while ( !binder->shouldStop() )
    {
        // The idea of this thread is to actively monitor all the channels and
        // connect / disconnect them according to their states.
        // We can think of running this in the main Engine thread in polling mode
        // instead of Interrupt mode if the engine knows exactly when Channels get disconnected.

        binder->mTimer.reset();
        uint32_t elapsedTimeUs = 0;
        binder->mWait.wait( Platform::Signal::WaitWithPredicate );
        elapsedTimeUs += static_cast<uint32_t>( binder->mTimer.getElapsedMs().count() );
        binder->mLogger.trace( "NetworkChannelBinder::doWork",
                               "Time Elapsed waiting for the interrupt : " + std::to_string( elapsedTimeUs ) );

        // Some Channels have been either connected or disconnected.
        // Copy the updates and release the lock so that other channels can
        // signal their updates.
        IdsToStates channelStatesCopy;
        {
            std::lock_guard<std::recursive_mutex> lock( binder->mChannelUpdatesMutex );
            if ( !binder->mChannelStates.empty() )
            {
                channelStatesCopy.swap( binder->mChannelStates );
            }
        }
        for ( auto &channelID : channelStatesCopy )
        {
            if ( channelID.second == CONNECTED )
            {
                // This is only successful if the Channel has been disconnected and reconnected.
                // On bootstrap, this will be simply ignored, as the channel is started
                // by the binder.
                if ( binder->reConnectConsumer( channelID.first ) )
                {
                    binder->mLogger.trace( "NetworkChannelBinder::doWork",
                                           "Reconnected Channel ID : " + std::to_string( channelID.first ) );
                }
            }
            else if ( channelID.second == DISCONNECTED )
            {
                // Channel is disconnected, we need to make sure the consumer is also disconnected
                if ( binder->disconnectConsumer( channelID.first ) )
                {
                    binder->mLogger.trace( "NetworkChannelBinder::doWork",
                                           "Disconnected Channel ID : " + std::to_string( channelID.first ) );
                }
            }
        }
    }
}

// This callback/Interrupt arrives from a different thread context. We just copy the
// interrupt and wake up this worker thread.
void
NetworkChannelBinder::onNetworkChannelConnected( const NetworkChannelID &id )
{
    // Check if the channel ID is valid, should not happen
    if ( id == INVALID_CHANNEL_ID )
    {
        return;
    }
    // This event happens if the channel has been disconnected before and got
    // externally re-connected. We should signal to this worker thread to rebind the
    // channel to underlying consumer
    {
        std::lock_guard<std::recursive_mutex> lock( mChannelUpdatesMutex );
        mChannelStates.emplace( id, CONNECTED );
    }
    // Send an Interrupt
    mWait.notify();
}

// This callback/Interrupt arrives from a different thread context. We just copy the
// interrupt and wake up this worker thread.
void
NetworkChannelBinder::onNetworkChannelDisconnected( const NetworkChannelID &id )
{
    // Check if the channel ID is valid, should not happen
    if ( id == INVALID_CHANNEL_ID )
    {
        return;
    }
    // This event happens if the channel has been disconnected.
    // We should signal to this worker thread to unbind the underlying consumer
    {
        std::lock_guard<std::recursive_mutex> lock( mChannelUpdatesMutex );
        mChannelStates.emplace( id, DISCONNECTED );
    }
    // Send an Interrupt
    mWait.notify();
}

bool
NetworkChannelBinder::connect()
{
    // Limiting the connect call to only starting the thread.
    // We could move the connect of all channels in this function, however it will limit
    // the runtime addition of channels, which might be a wanted feature in the future.
    // At this point, only the binder thread will start.
    return start();
}

bool
NetworkChannelBinder::disconnect()
{
    // In the disconnect, we need to make sure we disconnect all channels and consumers
    // in a safe way.
    // We start first disconnecting the consumers as they hold references to channels buffers.
    {
        // This mutex guarantees that when the consumers are disconnected,
        // this thread does not accidentally tries to reconnect them.
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        for ( auto const &consumer : mChannelsToConsumers )
        {
            if ( !consumer.second->disconnect() )
            {
                mLogger.error( "NetworkChannelBinder::disconnect", "Failed to disconnect Consumer" );
                return false;
            }
            else
            {
                mLogger.trace( "NetworkChannelBinder::disconnect", " Consumer disconnected" );
            }
        }
    }

    // In the disconnect, we need to make sure we disconnect all channels and consumers
    // in a safe way.
    {
        // This mutex guarantees that when the channels are disconnected,
        // this thread does not accidentally tries to reconnect them.
        std::lock_guard<std::mutex> lock( mChannelsMutex );
        for ( auto const &channel : mIdsToChannels )
        {
            if ( !channel.second->unSubscribeListener( this ) || !channel.second->disconnect() )
            {
                mLogger.error( "NetworkChannelBinder::disconnect",
                               "Failed to disconnect Channel ID : " + std::to_string( channel.first ) );
                return false;
            }
            else
            {
                mLogger.trace( "NetworkChannelBinder::disconnect",
                               "Channel ID : " + std::to_string( channel.first ) + " disconnected" );
            }
        }
    }
    // Finally, stop the thread
    return stop();
}

void
NetworkChannelBinder::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                                  NetworkChannelProtocol networkProtocol )
{
    // This callbacks arrives from the CollectionScheme Management thread and does the following :
    // 1- If there were no active dictionary available at all in the system, or we have a corrupt one,
    // the channels and consumers must go to sleep.
    // 2- If we receive a new manifest, we should wake up the channel and the consumer for the given
    // Network Channel type
    mLogger.trace( "NetworkChannelBinder::onChangeOfActiveDictionary", "Decoder Manifest received " );
    // Start with the consumers, make sure that wake up first so that they pick up
    // the data for decoding immediately

    // The critical section is kept on both channels and consumers
    // Just in case new channels and consumers are added while we are applying this
    // transformation.
    std::lock_guard<std::mutex> lockChannel( mChannelsMutex );
    std::lock_guard<std::mutex> lockConsumer( mConsumersMutex );
    if ( dictionary.get() != nullptr )
    {

        std::for_each( mChannelsToConsumers.begin(),
                       mChannelsToConsumers.end(),
                       [&]( const std::pair<NetworkChannelID, NetworkChannelConsumerPtr> &consumer ) {
                           if ( networkProtocol == consumer.second->getChannelProtocol() )
                           {
                               consumer.second->resumeDataConsumption( dictionary );
                               mLogger.trace( "NetworkChannelBinder::onChangeOfActiveDictionary",
                                              "Resuming Consumption on Consumer :" +
                                                  std::to_string( consumer.second->getConsumerID() ) );
                           }
                       } );

        std::for_each( mIdsToChannels.begin(),
                       mIdsToChannels.end(),
                       [&]( const std::pair<NetworkChannelID, NetworkChannelPtr> &channel ) {
                           if ( networkProtocol == channel.second->getChannelProtocol() )
                           {
                               channel.second->resumeDataAcquisition();
                               mLogger.trace( "NetworkChannelBinder::onChangeOfActiveDictionary",
                                              "Resuming Consumption on Channel : " +
                                                  std::to_string( channel.second->getChannelID() ) );
                           }
                       } );
    }
    else
    {

        std::for_each( mChannelsToConsumers.begin(),
                       mChannelsToConsumers.end(),
                       [&]( const std::pair<NetworkChannelID, NetworkChannelConsumerPtr> &consumer ) {
                           if ( networkProtocol == consumer.second->getChannelProtocol() )
                           {
                               consumer.second->suspendDataConsumption();
                               mLogger.trace( "NetworkChannelBinder::onChangeOfActiveDictionary",
                                              "Interrupting Consumption on Consumer :" +
                                                  std::to_string( consumer.second->getConsumerID() ) );
                           }
                       } );

        std::for_each( mIdsToChannels.begin(),
                       mIdsToChannels.end(),
                       [&]( const std::pair<NetworkChannelID, NetworkChannelPtr> &channel ) {
                           if ( networkProtocol == channel.second->getChannelProtocol() )
                           {
                               channel.second->suspendDataAcquisition();
                               mLogger.trace( "NetworkChannelBinder::onChangeOfActiveDictionary",
                                              "Interrupting Consumption on Channel : " +
                                                  std::to_string( channel.second->getChannelID() ) );
                           }
                       } );
    }
}
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
