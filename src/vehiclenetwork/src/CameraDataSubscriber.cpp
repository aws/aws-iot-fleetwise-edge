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
#include "dds/CameraDataSubscriber.h"
#include "ClockHandler.h"
#include <cstdio>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <fstream>
#include <iostream>
namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
CameraDataSubscriber::CameraDataSubscriber()
{
    mNetworkProtocol = VehicleDataSourceProtocol::DDS;
    mID = generateChannelID();
}

CameraDataSubscriber::~CameraDataSubscriber()
{
    // To make sure the thread stops during teardown of tests.
    if ( mThread.isValid() && mThread.isActive() )
    {
        stop();
    }

    // Clean up the ressources
    if ( mDDSReader != nullptr )
    {
        mDDSSubscriber->delete_datareader( mDDSReader );
    }

    if ( mDDSSubscriber != nullptr )
    {
        mDDSParticipant->delete_subscriber( mDDSSubscriber );
    }
    if ( mDDSTopic != nullptr )
    {
        mDDSParticipant->delete_topic( mDDSTopic );
    }
    DomainParticipantFactory::get_instance()->delete_participant( mDDSParticipant );
}

bool
CameraDataSubscriber::init( const DDSDataSourceConfig &dataSourceConfig )
{
    // DDS Settings
    mCachePath = dataSourceConfig.temporaryCacheLocation;
    DomainParticipantQos participantQos;
    participantQos.name( dataSourceConfig.readerName );
    // Configure the Transport
    // First disable the default Transport
    participantQos.transport().use_builtin_transports = false;
    // UDP Transport
    if ( dataSourceConfig.transportType == DDSTransportType::UDP )
    {
        auto udpTransport = std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
        udpTransport->sendBufferSize = SEND_BUFFER_SIZE_BYTES;
        udpTransport->receiveBufferSize = RECEIVE_BUFFER_SIZE_BYTES;
        udpTransport->non_blocking_send = true;
        participantQos.transport().user_transports.push_back( udpTransport );
    }
    // Shared Memory Transport
    else if ( dataSourceConfig.transportType == DDSTransportType::SHM )
    {
        std::shared_ptr<eprosima::fastdds::rtps::SharedMemTransportDescriptor> shm_transport =
            std::make_shared<eprosima::fastdds::rtps::SharedMemTransportDescriptor>();
        // Link the Transport Layer to the Participant.
        participantQos.transport().user_transports.push_back( shm_transport );
    }
    else if ( dataSourceConfig.transportType == DDSTransportType::TCP )
    {
        mLogger.trace( "CameraDataSubscriber::init", " TCP Transport is NOT yet supported " );
        return false;
    }
    // Create the DDS participant
    mDDSParticipant =
        DomainParticipantFactory::get_instance()->create_participant( dataSourceConfig.domainID, participantQos );

    if ( mDDSParticipant == nullptr )
    {
        return false;
    }
    // Register the DDS participant
    mDDStype.register_type( mDDSParticipant );

    // Create the DDS Topic.
    mDDSTopic = mDDSParticipant->create_topic( dataSourceConfig.subscribeTopicName,
                                               mDDStype.get_type_name(),
                                               /*dataSourceConfig.topicQoS*/ TOPIC_QOS_DEFAULT );
    if ( mDDSTopic == nullptr )
    {
        return false;
    }

    mDDSSubscriber = mDDSParticipant->create_subscriber( SUBSCRIBER_QOS_DEFAULT );
    if ( mDDSSubscriber == nullptr )
    {
        return false;
    }

    mDDSReader = mDDSSubscriber->create_datareader( mDDSTopic, DATAREADER_QOS_DEFAULT, this );
    if ( mDDSReader == nullptr )
    {
        return false;
    }
    // Store the source ID to be able to report on the right Source when we receive data.
    mSourceID = dataSourceConfig.sourceID;
    return true;
}

bool
CameraDataSubscriber::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "CameraDataSubscriber::start", " Camera Subscriber Thread failed to start " );
    }
    else
    {
        mLogger.trace( "CameraDataSubscriber::start", " Camera Subscriber Thread started " );
        mThread.setThreadName( "fwVNDDSCamSub" + std::to_string( mID ) );
    }
    return mThread.isActive() && mThread.isValid();
}

bool
CameraDataSubscriber::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "CameraDataSubscriber::stop", " Camera Subscriber Thread stopped " );
    return !mThread.isActive();
}

bool
CameraDataSubscriber::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
CameraDataSubscriber::doWork( void *data )
{

    CameraDataSubscriber *subscriber = static_cast<CameraDataSubscriber *>( data );

    while ( !subscriber->shouldStop() )
    {
        // Wait for data to arrive from the DDS Network.
        subscriber->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        // Ok we have received data and it should have been loaded into our CameraDataItem.
        // We should log it into local storage location and then notify the DDS Handler that
        // the data is ready.
        // Make sure that we only notify during normal cycle and NOT on shutdown
        if ( subscriber->mNewResponseReceived.load() )
        {

            SensorArtifactMetadata cameraArtifact;
            cameraArtifact.path = subscriber->mCachePath + subscriber->mDataItem.dataItemId();
            cameraArtifact.sourceID = subscriber->mSourceID;
            if ( Aws::IoTFleetWise::VehicleNetwork::CameraDataSubscriber::persistToStorage(
                     subscriber->mDataItem.frameBuffer(), cameraArtifact.path ) )
            {
                subscriber->notifyListeners<const SensorArtifactMetadata &>(
                    &SensorDataListener::onSensorArtifactAvailable, cameraArtifact );
                subscriber->mLogger.info( "CameraDataSubscriber::doWork",
                                          " Data Collected from the Camera and made available " );
            }
            else
            {
                subscriber->mLogger.error( "CameraDataSubscriber::doWork",
                                           " Could not persist the data received into disk " );
            }

            // Reset the response
            subscriber->mNewResponseReceived.store( false, std::memory_order_relaxed );
        }
    }
}

bool
CameraDataSubscriber::connect()
{
    return start();
}

bool
CameraDataSubscriber::disconnect()
{
    return stop();
}

bool
CameraDataSubscriber::isAlive()
{
    return ( mIsAlive.load( std::memory_order_relaxed ) && mThread.isValid() && mThread.isActive() );
}

void
CameraDataSubscriber::on_subscription_matched( DataReader *reader, const SubscriptionMatchedStatus &info )
{
    (void)reader; // unused variable

    if ( info.current_count_change == 1 )
    {
        mIsAlive.store( true, std::memory_order_relaxed );
        mLogger.trace( "CameraDataSubscriber::on_subscription_matched", " A publisher is available " );
    }
    else if ( info.current_count_change == -1 )
    {
        mIsAlive.store( false, std::memory_order_relaxed );
    }
}

void
CameraDataSubscriber::on_data_available( DataReader *reader )
{
    SampleInfo info;
    if ( reader->take_next_sample( &mDataItem, &info ) == ReturnCode_t::RETCODE_OK )
    {
        if ( info.valid_data )
        {
            mNewResponseReceived.store( true, std::memory_order_relaxed );
            mLogger.trace( "CameraDataSubscriber::on_data_available", " Data received from the DDS Node " );
            mWait.notify();
        }
    }
}

bool
CameraDataSubscriber::persistToStorage( const std::vector<CameraFrame> &frameBuffer, const std::string &fileName )
{
    // To be sure, cleanup any file with the same name
    (void)remove( fileName.c_str() );
    // First create the file
    std::ofstream newFile( fileName.c_str(), std::ios_base::binary | std::ios_base::app );
    if ( !newFile.is_open() )
    {
        return false;
    }
    else
    {
        // Iterate over all frames and append their data to the file.
        for ( const auto &frameData : frameBuffer )
        { // Write buffer to IO.
            newFile.write( reinterpret_cast<const char *>( frameData.frameData().data() ),
                           static_cast<std::streamsize>( frameData.frameData().size() ) );
        }

        newFile.close();
        return newFile.good();
    }
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
