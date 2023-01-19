
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dds/CameraDataPublisher.h"
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <functional>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

using namespace Aws::IoTFleetWise::VehicleNetwork;

class TestSubscriber : public DataReaderListener
{

public:
    TestSubscriber()
        : mTestParticipant( nullptr )
        , mTestSubscriber( nullptr )
        , mTestTopic( nullptr )
        , mTestReader( nullptr )
        , mTestType( new CameraDataRequestPubSubType() )
    {
    }

    ~TestSubscriber()
    {
        if ( mTestReader != nullptr )
        {
            mTestSubscriber->delete_datareader( mTestReader );
        }
        if ( mTestSubscriber != nullptr )
        {
            mTestParticipant->delete_subscriber( mTestSubscriber );
        }
        if ( mTestTopic != nullptr )
        {
            mTestParticipant->delete_topic( mTestTopic );
        }
        DomainParticipantFactory::get_instance()->delete_participant( mTestParticipant );
    }

    //! Initialize the publisher
    bool
    init( DDSTransportType type )
    {

        DomainParticipantQos participantQos;
        participantQos.name( "TestSubscriber" );
        participantQos.transport().use_builtin_transports = false;
        if ( type == DDSTransportType::UDP )
        {
            auto udpTransport = std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
            udpTransport->sendBufferSize = SEND_BUFFER_SIZE_BYTES;
            udpTransport->receiveBufferSize = RECEIVE_BUFFER_SIZE_BYTES;
            udpTransport->non_blocking_send = true;
            participantQos.transport().user_transports.push_back( udpTransport );
        }
        else if ( type == DDSTransportType::SHM )
        {
            std::shared_ptr<eprosima::fastdds::rtps::SharedMemTransportDescriptor> shm_transport =
                std::make_shared<eprosima::fastdds::rtps::SharedMemTransportDescriptor>();
            // Link the Transport Layer to the Participant.
            participantQos.transport().user_transports.push_back( shm_transport );
        }

        mTestParticipant = DomainParticipantFactory::get_instance()->create_participant( 0, participantQos );

        if ( mTestParticipant == nullptr )
        {
            return false;
        }

        // Register the Type
        mTestType.register_type( mTestParticipant );

        // Create the publications Topic
        mTestTopic = mTestParticipant->create_topic( "testTopic", mTestType.get_type_name(), TOPIC_QOS_DEFAULT );

        if ( mTestTopic == nullptr )
        {
            return false;
        }

        // Create the Subscriber
        mTestSubscriber = mTestParticipant->create_subscriber( SUBSCRIBER_QOS_DEFAULT );

        if ( mTestSubscriber == nullptr )
        {
            return false;
        }

        // Create the DataReader
        mTestReader = mTestSubscriber->create_datareader( mTestTopic, DATAREADER_QOS_DEFAULT, this );

        if ( mTestReader == nullptr )
        {
            return false;
        }
        return true;
    }

    void
    on_data_available( DataReader *reader ) override
    {
        SampleInfo info;
        reader->take_next_sample( &dataItem, &info );
    }

private:
    DomainParticipant *mTestParticipant;

    Subscriber *mTestSubscriber;

    Topic *mTestTopic;

    DataReader *mTestReader;

    TypeSupport mTestType;

public:
    CameraDataRequest dataItem;
};

/**
 * @brief Validate the life cycle of a Camera Data Publisher
 * 1- Create a default source config
 * 2- Assert that the Publisher is initialized
 * 3- As the Publisher is connected to the topic, but no subscriber is connected on the other side,
 *    the Publisher must be no alive.
 * 4- Trigger a notification that mocks a subscriber and assert that the Publisher is alive.
 * 5- Disconnect the Publisher
 */

TEST( CameraDataPublisherTest, testLifeCycle )
{
    CameraDataPublisher publisher;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testTopic",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    ASSERT_TRUE( publisher.init( config ) );
    ASSERT_TRUE( publisher.connect() );
    ASSERT_FALSE( publisher.isAlive() );
    PublicationMatchedStatus info;
    info.current_count_change = 1;
    publisher.on_publication_matched( nullptr, info );
    ASSERT_TRUE( publisher.isAlive() );
    ASSERT_TRUE( publisher.disconnect() );
    ASSERT_FALSE( publisher.isAlive() );
}

/**
 * @brief Validate the use case of sending data on the topic( based on UDP DDS Transport)
 *  of a CameraDataRequest over DDS using a mocked data.
 *  1- Create a default source config
 *  2- Assert that the Publisher is initialized but not alive
 *  3- Init a mock subscriber on the Topic
 *  4- Assert that the Publisher is Alive
 *  5- Send a request on the topic
 *  6- Assert that the Subscriber has received the exact same request
 *  7- Disconnect the Publisher
 */
TEST( CameraDataPublisherTest, testSendDataUPDTransport )
{
    CameraDataPublisher publisher;

    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testTopic",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "testWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::UDP

    };

    TestSubscriber subscriber;
    // Init the publisher
    ASSERT_TRUE( publisher.init( config ) );
    ASSERT_TRUE( publisher.connect() );
    // No subscriber connected, thus not Alive
    ASSERT_FALSE( publisher.isAlive() );
    // Init the subscriber and give some time for the DDS stack to notify the publisher
    ASSERT_TRUE( subscriber.init( DDSTransportType::UDP ) );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( publisher.isAlive() );

    // Publish data
    DDSDataRequest dataRequest{ 123, 1, 1 };
    publisher.publishDataRequest( dataRequest );
    // Give some time so that the Subscriber receives the message
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    // Verify that the data arrived
    ASSERT_EQ( subscriber.dataItem.dataItemId(), 123 );
    ASSERT_TRUE( publisher.disconnect() );
    ASSERT_FALSE( publisher.isAlive() );
}

/**
 * @brief Validate the use case of sending data on the topic( based on SHM DDS Transport)
 *  of a CameraDataRequest over DDS using a mocked data.
 *  1- Create a default source config
 *  2- Assert that the Publisher is initialized but not alive
 *  3- Init a mock subscriber on the Topic
 *  4- Assert that the Publisher is Alive
 *  5- Send a request on the topic
 *  6- Assert that the Subscriber has received the exact same request
 *  7- Disconnect the Publisher
 */
TEST( CameraDataPublisherTest, testSendDataSHMTransport )
{
    CameraDataPublisher publisher;

    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testTopic",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "testWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM

    };

    TestSubscriber subscriber;
    // Init the publisher
    ASSERT_TRUE( publisher.init( config ) );
    ASSERT_TRUE( publisher.connect() );
    // No subscriber connected, thus not Alive
    ASSERT_FALSE( publisher.isAlive() );
    // Init the subscriber and give some time for the DDS stack to notify the publisher
    ASSERT_TRUE( subscriber.init( DDSTransportType::SHM ) );
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( publisher.isAlive() );

    // Publish data
    DDSDataRequest dataRequest{ 123, 1, 1 };
    publisher.publishDataRequest( dataRequest );
    // Give some time so that the Subscriber receives the message
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    // Verify that the data arrived
    ASSERT_EQ( subscriber.dataItem.dataItemId(), 123 );
    ASSERT_TRUE( publisher.disconnect() );
    ASSERT_FALSE( publisher.isAlive() );
}
