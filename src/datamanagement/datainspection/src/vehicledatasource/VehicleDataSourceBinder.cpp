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
#include "VehicleDataSourceBinder.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

VehicleDataSourceBinder::~VehicleDataSourceBinder()
{
    mDataSourcesToConsumers.clear();
    mIdsToDataSources.clear();
    mDataSourceStates.clear();
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
VehicleDataSourceBinder::addVehicleDataSource( VehicleDataSourcePtr source )
{
    // Check if the data source is valid
    if ( source.get() == nullptr || source->getVehicleDataSourceID() == INVALID_DATA_SOURCE_ID )
    {
        mLogger.error( "VehicleDataSourceBinder::attachVehicleDataSource", "Invalid vehicle data source" );
        return false;
    }
    // Insert the Source if it's not already inserted
    {
        std::lock_guard<std::mutex> lock( mVehicleDataSourcesMutex );
        auto sourceIterator = mIdsToDataSources.emplace( source->getVehicleDataSourceID(), source );
        if ( !sourceIterator.second )
        {
            mLogger.error( "VehicleDataSourceBinder::attachVehicleDataSource",
                           "Could not add the vehicle data source to the binder instance" );
            return false;
        }
        else
        {
            mLogger.trace( "VehicleDataSourceBinder::attachVehicleDataSource",
                           "SourceID: " + std::to_string( source->getVehicleDataSourceID() ) + " added" );
        }
    }
    // Register Self for Connect and Disconnect Callbacks and connect the vehicle data source
    if ( source->connect() && source->subscribeListener( this ) )
    {
        return true;
    }
    else
    {
        mLogger.error( "VehicleDataSourceBinder::attachVehicleDataSource",
                       "Could not connect the  vehicle data source with ID: " +
                           std::to_string( source->getVehicleDataSourceID() ) );
        return false;
    }
}

bool
VehicleDataSourceBinder::removeVehicleDataSource( const VehicleDataSourceID &id )
{
    // Check if the data source is valid
    if ( id == INVALID_DATA_SOURCE_ID )
    {
        return false;
    }
    auto backupSource = VehicleDataSourcePtr();
    // Lookup the Channel in the internal list
    {
        std::lock_guard<std::mutex> lock( mVehicleDataSourcesMutex );
        auto sourceIterator = mIdsToDataSources.find( id );
        // Something went wrong... removing a data source that does not exist
        if ( sourceIterator == mIdsToDataSources.end() )
        {
            mLogger.error( "VehicleDataSourceBinder::removeVehicleDataSource",
                           "Attempting to remove a vehicle data source that was not added." );
            return false;
        }
        backupSource = sourceIterator->second;
        mIdsToDataSources.erase( sourceIterator );
    }
    // Deregister this thread from any of the callbacks of this data source and disconnect the data source
    if ( backupSource != nullptr )
    {
        if ( backupSource->unSubscribeListener( this ) && backupSource->disconnect() )
        {
            return true;
        }
        else
        {
            mLogger.error( "VehicleDataSourceBinder::removeVehicleDataSource",
                           "Could not disconnect the vehicle data source with ID: " +
                               std::to_string( backupSource->getVehicleDataSourceID() ) );
            return false;
        }
    }
    else
    {
        return false;
    }
}

bool
VehicleDataSourceBinder::bindConsumerToVehicleDataSource( VehicleDataConsumerPtr consumer,
                                                          const VehicleDataSourceID &id )
{
    // Check if the channelID and the consumer are valid
    if ( consumer.get() == nullptr || id == INVALID_DATA_SOURCE_ID )
    {
        mLogger.error( "VehicleDataSourceBinder::bindConsumerToVehicleDataSource",
                       "Invalid consumer instance  or data source" );
        return false;
    }
    // First lookup the data source and check if it's registered
    auto dataSourceIterator = mIdsToDataSources.find( id );
    if ( dataSourceIterator == mIdsToDataSources.end() )
    {
        mLogger.error( "VehicleDataSourceBinder::bindConsumerToVehicleDataSource", "Source not found" );
        return false;
    }
    // Insert the consumer/ID pair
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mDataSourcesToConsumers.emplace( id, consumer );

        if ( !consumerIterator.second )
        {
            return false;
        }
    }
    // Propagate the data source metadata
    consumer->setChannelMetadata( std::make_tuple( dataSourceIterator->second->getVehicleDataSourceType(),
                                                   dataSourceIterator->second->getVehicleDataSourceProtocol(),
                                                   dataSourceIterator->second->getVehicleDataSourceIfName() ) );
    // Assign the circular buffer of the data source to the consumer
    consumer->setInputBuffer( dataSourceIterator->second->getBuffer() );
    // Start the consumer worker
    return consumer->connect();
}

bool
VehicleDataSourceBinder::unBindConsumerFromVehicleDataSource( const VehicleDataSourceID &id )
{
    // Check if the data source ID is valid
    if ( id == INVALID_DATA_SOURCE_ID )
    {
        mLogger.error( "VehicleDataSourceBinder::unBindConsumerFromVehicleDataSource",
                       "Invalid consumer instance  or data source" );
        return false;
    }

    auto backupConsumer = VehicleDataConsumerPtr();
    // Lookup the consumer registered to this data source
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mDataSourcesToConsumers.find( id );
        // Something went wrong... No consumer is registered for this data source
        if ( consumerIterator == mDataSourcesToConsumers.end() )
        {
            mLogger.error( "VehicleDataSourceBinder::unBindConsumerFromVehicleDataSource", "Consumer not found" );
            return false;
        }
        backupConsumer = consumerIterator->second;
        mDataSourcesToConsumers.erase( consumerIterator );
    }
    // Disconnect the consumer.
    if ( backupConsumer != nullptr )
    {
        return backupConsumer->disconnect();
    }
    else
    {
        return false;
    }
}

bool
VehicleDataSourceBinder::disconnectConsumer( const VehicleDataSourceID &id )
{
    auto backupConsumer = VehicleDataConsumerPtr();
    // Lookup the consumer registered to this data source
    // release the Mutex after this context to allow addition of consumers.
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mDataSourcesToConsumers.find( id );
        // Something went wrong... No consumer is registered for this data source
        if ( consumerIterator == mDataSourcesToConsumers.end() )
        {
            mLogger.error( "VehicleDataSourceBinder::disconnectConsumer", "Consumer not found" );
            return false;
        }
        backupConsumer = consumerIterator->second;
    }
    // Disconnect the consumer.
    if ( backupConsumer != nullptr )
    {
        return backupConsumer->disconnect();
    }
    else
    {
        return false;
    }
}

bool
VehicleDataSourceBinder::reConnectConsumer( const VehicleDataSourceID &id )
{
    auto backupConsumer = VehicleDataConsumerPtr();
    // Lookup the consumer registered to this data source
    // release the Mutex after this context to allow addition of consumers.
    {
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        auto consumerIterator = mDataSourcesToConsumers.find( id );
        // Something went wrong... No consumer is registered for this data source
        if ( consumerIterator == mDataSourcesToConsumers.end() )
        {
            mLogger.error( "VehicleDataSourceBinder::reConnectConsumer", "Consumer not found" );
            return false;
        }
        backupConsumer = consumerIterator->second;
    }
    // reConnect the consumer. Make sure that the consumer is not alive already
    if ( backupConsumer != nullptr && !backupConsumer->isAlive() )
    {
        return backupConsumer->connect();
    }
    else
    {
        return false;
    }
}

bool
VehicleDataSourceBinder::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "VehicleDataSourceBinder::start", " Binder Thread failed to start " );
    }
    else
    {
        mLogger.trace( "VehicleDataSourceBinder::start", " Binder Thread started " );
        mThread.setThreadName( "fwDIBinder" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
VehicleDataSourceBinder::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
VehicleDataSourceBinder::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
VehicleDataSourceBinder::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

void
VehicleDataSourceBinder::doWork( void *data )
{

    VehicleDataSourceBinder *binder = static_cast<VehicleDataSourceBinder *>( data );
    while ( !binder->shouldStop() )
    {
        // The idea of this thread is to actively monitor all the channels and
        // connect / disconnect them according to their states.
        // We can think of running this in the main Engine thread in polling mode
        // instead of Interrupt mode if the engine knows exactly when Channels get disconnected.

        binder->mTimer.reset();
        uint32_t elapsedTimeUs = 0;
        binder->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        elapsedTimeUs += static_cast<uint32_t>( binder->mTimer.getElapsedMs().count() );
        binder->mLogger.trace( "VehicleDataSourceBinder::doWork",
                               "Time Elapsed waiting for the interrupt : " + std::to_string( elapsedTimeUs ) );

        // Some Channels have been either connected or disconnected.
        // Copy the updates and release the lock so that other channels can
        // signal their updates.
        IdsToStates sourceStatesCopy;
        {
            std::lock_guard<std::mutex> lock( binder->mVehicleDataSourceUpdatesMutex );
            if ( !binder->mDataSourceStates.empty() )
            {
                sourceStatesCopy.swap( binder->mDataSourceStates );
            }
        }
        for ( auto &sourceID : sourceStatesCopy )
        {
            if ( sourceID.second == VehicleDataSourceState::CONNECTED )
            {
                // This is only successful if the source has been disconnected and reconnected.
                // On bootstrap, this will be simply ignored, as the data source is started
                // by the binder.
                if ( binder->reConnectConsumer( sourceID.first ) )
                {
                    binder->mLogger.trace( "VehicleDataSourceBinder::doWork",
                                           "Reconnected Source ID : " + std::to_string( sourceID.first ) );
                }
            }
            else if ( sourceID.second == VehicleDataSourceState::DISCONNECTED )
            {
                // Data source is disconnected, we need to make sure the consumer is also disconnected
                if ( binder->disconnectConsumer( sourceID.first ) )
                {
                    binder->mLogger.trace( "VehicleDataSourceBinder::doWork",
                                           "Disconnected Source ID : " + std::to_string( sourceID.first ) );
                }
            }
        }
    }
}

// This callback/Interrupt arrives from a different thread context. We just copy the
// interrupt and wake up this worker thread.
void
VehicleDataSourceBinder::onVehicleDataSourceConnected( const VehicleDataSourceID &id )
{
    // Check if the data source ID is valid, should not happen
    if ( id == INVALID_DATA_SOURCE_ID )
    {
        return;
    }
    // This event happens if the data source has been disconnected before and got
    // externally re-connected. We should signal to this worker thread to rebind the
    // data source to underlying consumer
    {
        std::lock_guard<std::mutex> lock( mVehicleDataSourceUpdatesMutex );
        mDataSourceStates.emplace( id, VehicleDataSourceState::CONNECTED );
    }
    // Send an Interrupt
    mWait.notify();
}

// This callback/Interrupt arrives from a different thread context. We just copy the
// interrupt and wake up this worker thread.
void
VehicleDataSourceBinder::onVehicleDataSourceDisconnected( const VehicleDataSourceID &id )
{
    // Check if the data source ID is valid, should not happen
    if ( id == INVALID_DATA_SOURCE_ID )
    {
        return;
    }
    // This event happens if the data source has been disconnected.
    // We should signal to this worker thread to unbind the underlying consumer
    {
        std::lock_guard<std::mutex> lock( mVehicleDataSourceUpdatesMutex );
        mDataSourceStates.emplace( id, VehicleDataSourceState::DISCONNECTED );
    }
    // Send an Interrupt
    mWait.notify();
}

bool
VehicleDataSourceBinder::connect()
{
    // Limiting the connect call to only starting the thread.
    // We could move the connect of all channels in this function, however it will limit
    // the runtime addition of channels, which might be a wanted feature in the future.
    // At this point, only the binder thread will start.
    return start();
}

bool
VehicleDataSourceBinder::disconnect()
{
    // In the disconnect, we need to make sure we disconnect all channels and consumers
    // in a safe way.
    // We start first disconnecting the consumers as they hold references to channels buffers.
    {
        // This mutex guarantees that when the consumers are disconnected,
        // this thread does not accidentally tries to reconnect them.
        std::lock_guard<std::mutex> lock( mConsumersMutex );
        for ( auto const &consumer : mDataSourcesToConsumers )
        {
            if ( !consumer.second->disconnect() )
            {
                mLogger.error( "VehicleDataSourceBinder::disconnect", "Failed to disconnect Consumer" );
                return false;
            }
            else
            {
                mLogger.trace( "VehicleDataSourceBinder::disconnect", " Consumer disconnected" );
            }
        }
    }

    // In the disconnect, we need to make sure we disconnect all channels and consumers
    // in a safe way.
    {
        // This mutex guarantees that when the channels are disconnected,
        // this thread does not accidentally tries to reconnect them.
        std::lock_guard<std::mutex> lock( mVehicleDataSourcesMutex );
        for ( auto const &source : mIdsToDataSources )
        {
            //            if ( !source.second->unSubscribeListener( this ) || !source.second->disconnect() )
            if ( !source.second->disconnect() )
            {
                mLogger.error( "VehicleDataSourceBinder::disconnect",
                               "Failed to disconnect Data source ID : " + std::to_string( source.first ) );
                return false;
            }
            else
            {
                mLogger.trace( "VehicleDataSourceBinder::disconnect",
                               "Data source ID : " + std::to_string( source.first ) + " disconnected" );
            }
        }
    }
    // Finally, stop the thread
    return stop();
}

void
VehicleDataSourceBinder::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                                     VehicleDataSourceProtocol networkProtocol )
{
    // This callbacks arrives from the CollectionScheme Management thread and does the following :
    // 1- If there were no active dictionary available at all in the system, or we have a corrupt one,
    // the channels and consumers must go to sleep.
    // 2- If we receive a new manifest, we should wake up the data source and the consumer for the given
    // Vehicle Data Consumer type
    mLogger.trace( "VehicleDataSourceBinder::onChangeOfActiveDictionary", "Decoder Manifest received " );
    // Start with the consumers, make sure that wake up first so that they pick up
    // the data for decoding immediately

    // The critical section is kept on both channels and consumers
    // Just in case new channels and consumers are added while we are applying this
    // transformation.
    std::lock_guard<std::mutex> lockChannel( mVehicleDataSourcesMutex );
    std::lock_guard<std::mutex> lockConsumer( mConsumersMutex );
    if ( dictionary.get() != nullptr )
    {

        std::for_each( mDataSourcesToConsumers.begin(),
                       mDataSourcesToConsumers.end(),
                       [&]( const std::pair<VehicleDataSourceID, VehicleDataConsumerPtr> &consumer ) {
                           if ( networkProtocol == consumer.second->getVehicleDataSourceProtocol() )
                           {
                               consumer.second->resumeDataConsumption( dictionary );
                               mLogger.trace( "VehicleDataSourceBinder::onChangeOfActiveDictionary",
                                              "Resuming Consumption on Consumer :" +
                                                  std::to_string( consumer.second->getConsumerID() ) );
                           }
                       } );

        std::for_each( mIdsToDataSources.begin(),
                       mIdsToDataSources.end(),
                       [&]( const std::pair<VehicleDataSourceID, VehicleDataSourcePtr> &source ) {
                           if ( networkProtocol == source.second->getVehicleDataSourceProtocol() )
                           {
                               source.second->resumeDataAcquisition();
                               mLogger.trace( "VehicleDataSourceBinder::onChangeOfActiveDictionary",
                                              "Resuming Consumption on Data source : " +
                                                  std::to_string( source.second->getVehicleDataSourceID() ) );
                           }
                       } );
    }
    else
    {

        std::for_each( mDataSourcesToConsumers.begin(),
                       mDataSourcesToConsumers.end(),
                       [&]( const std::pair<VehicleDataSourceID, VehicleDataConsumerPtr> &consumer ) {
                           if ( networkProtocol == consumer.second->getVehicleDataSourceProtocol() )
                           {
                               consumer.second->suspendDataConsumption();
                               mLogger.trace( "VehicleDataSourceBinder::onChangeOfActiveDictionary",
                                              "Interrupting Consumption on Consumer :" +
                                                  std::to_string( consumer.second->getConsumerID() ) );
                           }
                       } );

        std::for_each( mIdsToDataSources.begin(),
                       mIdsToDataSources.end(),
                       [&]( const std::pair<VehicleDataSourceID, VehicleDataSourcePtr> &source ) {
                           if ( networkProtocol == source.second->getVehicleDataSourceProtocol() )
                           {
                               source.second->suspendDataAcquisition();
                               mLogger.trace( "VehicleDataSourceBinder::onChangeOfActiveDictionary",
                                              "Interrupting Consumption on Data source : " +
                                                  std::to_string( source.second->getVehicleDataSourceID() ) );
                           }
                       } );
    }
}
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
