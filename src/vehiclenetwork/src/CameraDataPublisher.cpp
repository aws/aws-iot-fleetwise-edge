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
#include "dds/CameraDataPublisher.h"
#include "ClockHandler.h"
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{
CameraDataPublisher::CameraDataPublisher()
{
    mNetworkProtocol = VehicleDataSourceProtocol::DDS;
    mID = generateChannelID();
}

CameraDataPublisher::~CameraDataPublisher()
{
    // To make sure the thread stops during teardown of tests.
    if ( mThread.isValid() && mThread.isActive() )
    {
        stop();
    }

    // Clean up the ressources
    if ( mDDSWriter != nullptr )
    {
        mDDSPublisher->delete_datawriter( mDDSWriter );
    }

    if ( mDDSPublisher != nullptr )
    {
        mDDSParticipant->delete_publisher( mDDSPublisher );
    }
    if ( mDDSTopic != nullptr )
    {
        mDDSParticipant->delete_topic( mDDSTopic );
    }

    DomainParticipantFactory::get_instance()->delete_participant( mDDSParticipant );
}

bool
CameraDataPublisher::init( const DDSDataSourceConfig &dataSourceConfig )
{

    // DDS Settings
    DomainParticipantQos participantQos;
    participantQos.name( dataSourceConfig.writerName );
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
        mLogger.trace( "CameraDataPublisher::init", " TCP Transport is NOT yet supported " );
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
    mDDSTopic = mDDSParticipant->create_topic(
        dataSourceConfig.publishTopicName, mDDStype.get_type_name(), /*dataSourceConfig.topicQoS*/ TOPIC_QOS_DEFAULT );
    if ( mDDSTopic == nullptr )
    {
        return false;
    }

    mDDSPublisher = mDDSParticipant->create_publisher( PUBLISHER_QOS_DEFAULT );
    if ( mDDSPublisher == nullptr )
    {
        return false;
    }

    mDDSWriter = mDDSPublisher->create_datawriter( mDDSTopic, DATAWRITER_QOS_DEFAULT, this );
    if ( mDDSWriter == nullptr )
    {
        return false;
    }
    return true;
}

bool
CameraDataPublisher::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "CameraDataPublisher::start", " Camera Publisher Thread failed to start " );
    }
    else
    {
        mLogger.trace( "CameraDataPublisher::start", " Camera Publisher Thread started " );
        mThread.setThreadName( "fwVNDDSCamPub" + std::to_string( mID ) );
    }
    return mThread.isActive() && mThread.isValid();
}

bool
CameraDataPublisher::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "CameraDataPublisher::stop", " Camera Publisher Thread stopped " );
    return !mThread.isActive();
}

bool
CameraDataPublisher::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
CameraDataPublisher::doWork( void *data )
{

    CameraDataPublisher *publisher = static_cast<CameraDataPublisher *>( data );

    while ( !publisher->shouldStop() )
    {
        // Wait for data to arrive from the DDS Network.
        publisher->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        // We need now to send the request.
        // Reset the request to avoid sending it during shutdown
        std::lock_guard<std::mutex> lock( publisher->mRequesMutex );
        if ( !publisher->mRequestCompleted.load() )
        {
            publisher->mDDSWriter->write( &publisher->mRequest );
            publisher->mRequestCompleted.store( true );
            publisher->mLogger.trace( "CameraDataPublisher::doWork", " Data request send to the remote node " );
        }
    }
}

bool
CameraDataPublisher::connect()
{

    return start();
}

bool
CameraDataPublisher::disconnect()
{
    return stop();
}

bool
CameraDataPublisher::isAlive()
{
    return ( mIsAlive.load( std::memory_order_relaxed ) && mThread.isValid() && mThread.isActive() );
}

void
CameraDataPublisher::on_publication_matched( DataWriter *writer, const PublicationMatchedStatus &info )
{
    (void)writer; // unused variable

    if ( info.current_count_change == 1 )
    {
        mIsAlive.store( true, std::memory_order_relaxed );
        mLogger.trace( "CameraDataPublisher::on_publication_matched", " A subscriber is available " );
    }
    else if ( info.current_count_change == -1 )
    {
        mIsAlive.store( false, std::memory_order_relaxed );
    }
}

void
CameraDataPublisher::publishDataRequest( const DDSDataRequest &dataRequest )
{
    // Critical section only to make sure that we don't overwrite the
    // ongoing request processing in doWork.
    std::lock_guard<std::mutex> lock( mRequesMutex );
    {
        mRequest.dataItemId( dataRequest.eventID );
        mRequest.positiveOffsetMs( dataRequest.positiveOffsetMs );
        mRequest.negativeOffsetMs( dataRequest.negativeOffsetMs );
        mRequestCompleted.store( false );
    }

    mLogger.trace( "CameraDataPublisher::publishDataRequest", " Request queued for sending " );
    mWait.notify();
}

} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
