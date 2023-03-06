// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "DataOverDDSModule.h"
#include "LoggingModule.h"
#include "dds/CameraDataPublisher.h"
#include "dds/CameraDataSubscriber.h"
#include <bitset>
#include <iostream>
#include <iterator>
namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

DataOverDDSModule::~DataOverDDSModule()
{
    // To make sure the thread stops during teardown of tests.
    if ( mThread.isValid() && mThread.isActive() )
    {
        stop();
    }
}

bool
DataOverDDSModule::init( const DDSDataSourcesConfig &ddsDataSourcesConfig )
{
    // For each source config provided, create exactly one subscriber
    // and one Publisher.
    // We need to store the sourceID for each Publisher to be able to find it
    // when we receive a new event.
    // Currently, each subscriber and publisher have their own DDS Participant.
    // We could think of consolidating these into 1 single participant, but this
    // seoeration is currently intended, because we don't want this layer
    // of the stack to know about specific DDS Lib supplier types, and rather want
    // to stay abstract.

    DDSPublisherPtr publisher;
    DDSSubscriberPtr subscriber;
    for ( auto &config : ddsDataSourcesConfig )
    {
        switch ( config.sourceType )
        {
        case SensorSourceType::CAMERA:
            publisher = std::make_unique<CameraDataPublisher>();
            subscriber = std::make_unique<CameraDataSubscriber>();
            if ( ( !publisher->init( config ) ) || ( !subscriber->init( config ) ) )
            {
                FWE_LOG_ERROR( "Failed to init the Publisher/Subscriber" );
                return false;
            }
            else
            {
                // Store these nodes
                {
                    std::lock_guard<std::mutex> lock( mPubSubMutex );
                    mPublishers.emplace( config.sourceID, std::move( publisher ) );
                    mSubscribers.emplace_back( std::move( subscriber ) );
                }

                FWE_LOG_INFO( "Camera Publisher/Subscriber successfully initialised" );
            }
            break;

        default:
            FWE_LOG_WARN( "Not supported SensorType" );
            break;
        }
    }
    return ( !mPublishers.empty() ) && ( !mSubscribers.empty() ) && ( mPublishers.size() == mSubscribers.size() );
}

bool
DataOverDDSModule::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        FWE_LOG_TRACE( "DataOverDDSModule Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "DataOverDDSModule Thread started" );
        mThread.setThreadName( "fwDIDDSModule" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
DataOverDDSModule::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
DataOverDDSModule::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
DataOverDDSModule::doWork( void *data )
{
    DataOverDDSModule *DDSModule = static_cast<DataOverDDSModule *>( data );
    while ( !DDSModule->shouldStop() )
    {
        // Always go to a busy wait state and stay there till a notify comes.
        DDSModule->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        // We are now awake because there is an event notification from the inspection Engine.
        // We need to interpret the event metadata and find out which Source we need to
        // communicate with, and then invoke the corresponding publisher.
        // Make sure we don't do anything during a shutdown cycle
        if ( DDSModule->mNewEventReceived.load() )
        {
            {
                // We don't want a disconnect or connect to change the content of our container.
                std::lock_guard<std::mutex> lockPubSub( DDSModule->mPubSubMutex );
                // We need to iterate through all the items in the event and request every device
                // listed in there.
                // Critical section only to make sure the request content
                // is atomic e.g. not changed when this thread wakes up.
                std::lock_guard<std::mutex> lockEvent( DDSModule->mEventMetaMutex );
                for ( const auto &eventItem : DDSModule->mEventMetatdata )
                {

                    auto publishIterator = DDSModule->mPublishers.find( eventItem.sourceID );
                    // Something went wrong... The sourceID requested is not available
                    if ( publishIterator == DDSModule->mPublishers.end() )
                    {
                        // Hmm, we have received a notification to request data from a source that's
                        // not configured. We should log an error and skip the event.
                        FWE_LOG_ERROR( "Received an event for a Source that's not configured, Source ID: " +
                                       std::to_string( eventItem.sourceID ) );
                    }
                    else
                    {
                        // Okey, now we know there is a new event, forward it to the publisher
                        DDSDataRequest request = {};
                        request.eventID = eventItem.eventID;
                        request.negativeOffsetMs = eventItem.negativeOffsetMs;
                        request.positiveOffsetMs = eventItem.positiveOffsetMs;

                        // Go ahead and request the data from the underlying source
                        publishIterator->second->publishDataRequest( request );
                        FWE_LOG_TRACE(

                            "Send a request to the DDS Network upon eventID: " + std::to_string( eventItem.eventID ) +
                            " to DeviceID " + std::to_string( eventItem.sourceID ) );
                    }
                }
            }
            // Reset the event received and go back to a wait state
            DDSModule->mNewEventReceived.store( false );
        }
    }
}

bool
DataOverDDSModule::connect()
{

    {
        std::lock_guard<std::mutex> lock( mPubSubMutex );
        // First connect the Subscribers and the Publishers
        for ( auto &sub : mSubscribers )
        {
            // Register the module as a listener of the Subscriber
            if ( ( !sub->subscribeListener( this ) ) || ( !sub->connect() ) )
            {
                FWE_LOG_ERROR( "Failed to connect Subscriber" );
                return false;
            }
            else
            {
                FWE_LOG_TRACE( "Subscriber connected" );
            }
        }

        for ( auto &pub : mPublishers )
        {
            if ( !pub.second->connect() )
            {
                FWE_LOG_ERROR( "Failed to connect Publisher" );
                return false;
            }
            else
            {
                FWE_LOG_TRACE( "Publisher connected" );
            }
        }
    }

    // Then start the worker thread
    return start();
}

bool
DataOverDDSModule::disconnect()
{
    {
        std::lock_guard<std::mutex> lock( mPubSubMutex );
        // First disconnect the Subscribers and the Publishers
        for ( auto &sub : mSubscribers )
        {
            if ( ( !sub->unSubscribeListener( this ) ) || ( !sub->disconnect() ) )
            {
                FWE_LOG_ERROR( "Failed to disconnect Subscriber" );
                return false;
            }
            else
            {
                FWE_LOG_TRACE( "Subscriber disconnected" );
            }
        }

        for ( auto &pub : mPublishers )
        {
            if ( !pub.second->disconnect() )
            {
                FWE_LOG_ERROR( "Failed to disconnect Publisher" );
                return false;
            }
            else
            {
                FWE_LOG_TRACE( "Publisher disconnected" );
            }
        }
    }
    // Stop the worker thread
    return stop();
}

bool
DataOverDDSModule::isAlive()
{
    // Check that all Subscribers and Publishers are alive
    std::lock_guard<std::mutex> lock( mPubSubMutex );
    {
        for ( auto &sub : mSubscribers )
        {
            if ( !sub->isAlive() )
            {
                FWE_LOG_ERROR( "Subscriber not alive" );
                return false;
            }
        }

        for ( auto &pub : mPublishers )
        {
            if ( !pub.second->isAlive() )
            {
                FWE_LOG_ERROR( "Publisher not alive" );
                return false;
            }
        }
    }

    // This thread should be alive
    return ( mThread.isValid() && mThread.isActive() );
}

void
DataOverDDSModule::onEventOfInterestDetected( const std::vector<EventMetadata> &eventMetadata )
{
    // This runs in the context of the Inspection thread.
    // We don't have an event loop at we manage a single event at a time.
    // We don't need to guard for thread safety as this notification comes
    // from the Inspection thread only, but we still guard as the main loop
    // might be running an ongoing request.
    FWE_LOG_TRACE( "Received a new event" );
    std::lock_guard<std::mutex> lock( mEventMetaMutex );
    {
        mEventMetatdata = eventMetadata;
        mNewEventReceived.store( true );
    }
    // Wake up the thread
    mWait.notify();
}

void
DataOverDDSModule::onSensorArtifactAvailable( const SensorArtifactMetadata &artifactMetadata )
{
    static_cast<void>( artifactMetadata );
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
