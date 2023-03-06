
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dds/CameraDataSubscriber.h"
#include "WaitUntil.h"
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

using namespace Aws::IoTFleetWise::TestingSupport;
using namespace Aws::IoTFleetWise::VehicleNetwork;

/**
 * @brief Utlity function to check if the input and output files
 * that we send and receive are identical.
 * @param input the png we send from the publisher side
 * @param output the png we received on the subscriber side
 * @return true
 * @return false
 */
bool
areIdentical( const std::string &input, const std::string &output )
{
    std::ifstream inputFD( input, std::ifstream::ate | std::ifstream::binary );
    std::ifstream outputFD( output, std::ifstream::ate | std::ifstream::binary );

    // Check first the size
    if ( inputFD.tellg() != outputFD.tellg() )
    {
        return false;
    }
    // Rewind
    inputFD.seekg( 0 );
    outputFD.seekg( 0 );

    return std::equal( std::istreambuf_iterator<char>( inputFD ),
                       std::istreambuf_iterator<char>(),
                       std::istreambuf_iterator<char>( outputFD ) );
}
class localSensorDataListener : public SensorDataListener
{
public:
    localSensorDataListener()
        : gotNotification( false )
    {
    }
    inline void
    onSensorArtifactAvailable( const SensorArtifactMetadata &artifactMetadata )
    {
        artifact = artifactMetadata;
        gotNotification = true;
    }

    SensorArtifactMetadata artifact;
    bool gotNotification;
};

class TestPublisher
{

public:
    TestPublisher()
        : mTestParticipant( nullptr )
        , mTestPublisher( nullptr )
        , mTestTopic( nullptr )
        , mTestWriter( nullptr )
        , mTestType( new CameraDataItemPubSubType() )
    {
    }

    ~TestPublisher()
    {
        if ( mTestWriter != nullptr )
        {
            mTestPublisher->delete_datawriter( mTestWriter );
        }
        if ( mTestPublisher != nullptr )
        {
            mTestParticipant->delete_publisher( mTestPublisher );
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
        participantQos.name( "TestPublisher" );
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

        // Create the Publisher
        mTestPublisher = mTestParticipant->create_publisher( PUBLISHER_QOS_DEFAULT );

        if ( mTestPublisher == nullptr )
        {
            return false;
        }

        // Create the DataWriter
        mTestWriter = mTestPublisher->create_datawriter( mTestTopic, DATAWRITER_QOS_DEFAULT );

        if ( mTestWriter == nullptr )
        {
            return false;
        }
        return true;
    }

    void
    publishTestData( const std::string &id )
    {
        mTestItem.dataItemId( id );
        mTestWriter->write( &mTestItem );
    }

    void
    publishTestDataWithRealPNG( const std::string &id, const std::string &filePath )
    {
        mTestItem.dataItemId( id );
        std::ifstream file( filePath.c_str(), std::ios_base::binary | std::ios_base::in );
        struct stat res;
        size_t pngSize = 0;
        if ( file.is_open() )
        {

            if ( stat( filePath.c_str(), &res ) == 0 )
            {
                pngSize = static_cast<size_t>( res.st_size );
            }
            // Try to create frames of size of 1kb each.
            std::unique_ptr<uint8_t[]> readBufPtr( new uint8_t[pngSize]() );
            file.read( reinterpret_cast<char *>( readBufPtr.get() ), pngSize );
            std::vector<CameraFrame> frameBuffer;

            CameraFrame frame;
            std::vector<uint8_t> frameData( readBufPtr.get(), readBufPtr.get() + pngSize );
            frame.frameData( frameData );
            frameBuffer.emplace_back( frame );
            mTestItem.frameBuffer( frameBuffer );
            mTestWriter->write( &mTestItem );
        }
    }

private:
    CameraDataItem mTestItem;

    DomainParticipant *mTestParticipant;

    Publisher *mTestPublisher;

    Topic *mTestTopic;

    DataWriter *mTestWriter;

    TypeSupport mTestType;
};

/**
 * @brief Validate the life cycle of a Camera Data Subscriber
 * 1- Create a default source config
 * 2- Assert that the Subscriber is initialized
 * 3- As the subscriber is connected to the topic, but no pulisher is connected on the other side,
 *    the subscriber must be no alive.
 * 4- Trigger a notification that mocks a publisher and assert that the subscriber is alive.
 * 5- Disconnect the subscriber
 */
TEST( CameraDataSubscriberTest, testLifeCycle )
{
    CameraDataSubscriber subscriber;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "Test",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "Test",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    ASSERT_TRUE( subscriber.init( config ) );
    ASSERT_TRUE( subscriber.connect() );
    ASSERT_FALSE( subscriber.isAlive() );
    SubscriptionMatchedStatus info;
    info.current_count_change = 1;
    subscriber.on_subscription_matched( nullptr, info );
    ASSERT_TRUE( subscriber.isAlive() );
    ASSERT_TRUE( subscriber.disconnect() );
    ASSERT_FALSE( subscriber.isAlive() );
}

/**
 * @brief Validate the data retrieval ( based on UDP DDS Transport)
 *  of a CameraDataItem over DDS using a mocked data publisher.
 *  1- Create a default source config
 *  2- Assert that the Subscriber is initialized
 *  3- Register a listener to the subscriber to get notified when it has received data.
 *  4- Create a mocked publisher and attach it to the same topic.
 *  5- Validate that the subscriber detected this publisher via an isAlive check.
 *  6- Publish a message on the Topic
 *  7- Assert that the Subscriber received the message
 *  8- Assert that the Subscriber propagated the data to the listener
 *  9- Disconnect the subscriber
 */

TEST( CameraDataSubscriberTest, testReceiveDataUPDTransport )
{
    CameraDataSubscriber subscriber;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "",
                                   "./",
                                   DDSTransportType::UDP

    };
    ASSERT_TRUE( subscriber.init( config ) );
    ASSERT_TRUE( subscriber.connect() );
    ASSERT_FALSE( subscriber.isAlive() );
    localSensorDataListener dataListener;
    subscriber.subscribeListener( &dataListener );
    TestPublisher publisher;
    ASSERT_TRUE( publisher.init( DDSTransportType::UDP ) );
    // Give some time so that the DDS Stack gets loaded and the multicast gets agreed.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    // Send one message
    publisher.publishTestData( "dataItem1" );
    // Give some time for the worker to wake up and consume the message.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( subscriber.isAlive() );
    ASSERT_TRUE( dataListener.gotNotification );
    // Current implementation simply appaends the text to the base path.
    // This will be the actual path of the camera data once we implement storage.
    ASSERT_EQ( dataListener.artifact.path, "./dataItem1" );

    ASSERT_TRUE( subscriber.disconnect() );
    ASSERT_FALSE( subscriber.isAlive() );
}

/**
 * @brief Validate the data retrieval ( based on Shared Memory DDS Transport)
 * of a CameraDataItem over DDS using a mocked data publisher.
 *  1- Create a default source config
 *  2- Assert that the Subscriber is initialized
 *  3- Register a listener to the subscriber to get notified when it has received data.
 *  4- Create a mocked publisher and attach it to the same topic.
 *  5- Validate that the subscriber detected this publisher via an isAlive check.
 *  6- Publish a message of the Topic
 *  7- Assert that the Subscriber received the message
 *  8- Assert that the Subscriber propagated the data to the listener
 *  9- Disconnect the subscriber
 */
TEST( CameraDataSubscriberTest, testReceiveDataSHMTransport )
{
    CameraDataSubscriber subscriber;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "",
                                   "./",
                                   DDSTransportType::SHM

    };
    ASSERT_TRUE( subscriber.init( config ) );
    ASSERT_TRUE( subscriber.connect() );
    ASSERT_FALSE( subscriber.isAlive() );
    localSensorDataListener dataListener;
    subscriber.subscribeListener( &dataListener );
    TestPublisher publisher;
    ASSERT_TRUE( publisher.init( DDSTransportType::SHM ) );
    // Give some time so that the DDS Stack gets loaded and the multicast gets agreed.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    // Send one message
    publisher.publishTestData( "dataItem1" );
    // Give some time for the worker to wake up and consume the message.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( subscriber.isAlive() );
    ASSERT_TRUE( dataListener.gotNotification );
    // Current implementation simply appaends the text to the base path.
    // This will be the actual path of the camera data once we implement storage.
    ASSERT_EQ( dataListener.artifact.path, "./dataItem1" );

    ASSERT_TRUE( subscriber.disconnect() );
    ASSERT_FALSE( subscriber.isAlive() );
}

/**
 * @brief Validate the data retrieval ( based on Shared Memory DDS Transport)
 * of a CameraDataItem over DDS using a mocked real publisher.
 *  1- Create a default source config
 *  2- Assert that the Subscriber is initialized
 *  3- Register a listener to the subscriber to get notified when it has received data.
 *  4- Create a mocked publisher and attach it to the same topic.
 *  5- Validate that the subscriber detected this publisher via an isAlive check.
 *  6- Publish a message of the Topic. The message consists of a PNG file.
 *  7- Assert that the Subscriber received the message
 *  8- Assert that the Subscriber propagated the data to the listener
 *  9- Check that the PNG data send is exactly the same received.
 *  9- Disconnect the subscriber
 */
TEST( CameraDataSubscriberTest, testReceiveDataAndPersistSHM )
{
    char currentDir[PATH_MAX];
    if ( getcwd( currentDir, sizeof( currentDir ) ) != NULL )
    {
        CameraDataSubscriber subscriber;
        DDSDataSourceConfig config = { 1,
                                       SensorSourceType::CAMERA,
                                       0,
                                       "",
                                       "testTopic",
                                       "TOPIC_QOS_DEFAULT",
                                       "testReader",
                                       "",
                                       std::string( currentDir ) + "/",
                                       DDSTransportType::SHM

        };
        ASSERT_TRUE( subscriber.init( config ) );

        ASSERT_TRUE( subscriber.connect() );
        ASSERT_FALSE( subscriber.isAlive() );
        localSensorDataListener dataListener;
        subscriber.subscribeListener( &dataListener );
        TestPublisher publisher;
        ASSERT_TRUE( publisher.init( DDSTransportType::SHM ) );
        // Give some time so that the DDS Stack gets loaded and the multicast gets agreed.
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        ASSERT_TRUE( subscriber.isAlive() );
        // Send one message
        publisher.publishTestDataWithRealPNG( "testPersitSHM.png",
                                              std::string( currentDir ) + "/CameraSubscriberTestPNG.png" );
        // Give some time for the worker to wake up and consume the message.
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        ASSERT_TRUE( dataListener.gotNotification );
        ASSERT_EQ( dataListener.artifact.path, config.temporaryCacheLocation + "testPersitSHM.png" );
        // Verify that  what we send on DDS is exactly what we received i.e the same PNG file.
        ASSERT_TRUE(
            areIdentical( std::string( currentDir ) + "/CameraSubscriberTestPNG.png", dataListener.artifact.path ) );
        ASSERT_TRUE( subscriber.disconnect() );
        ASSERT_FALSE( subscriber.isAlive() );
    }
    else
    {
        std::cout << "Could not find current working directory" << std::endl;
    }
}

/**
 * @brief Validate the data retrieval ( based on UDP DDS Transport)
 * of a CameraDataItem over DDS using a mocked real publisher.
 *  1- Create a default source config
 *  2- Assert that the Subscriber is initialized
 *  3- Register a listener to the subscriber to get notified when it has received data.
 *  4- Create a mocked publisher and attach it to the same topic.
 *  5- Validate that the subscriber detected this publisher via an isAlive check.
 *  6- Publish a message of the Topic. The message consists of a PNG file.
 *  7- Assert that the Subscriber received the message
 *  8- Assert that the Subscriber propagated the data to the listener
 *  9- Check that the PNG data send is exactly the same received.
 *  9- Disconnect the subscriber
 */
TEST( CameraDataSubscriberTest, testReceiveDataAndPersistUDP )
{
    char currentDir[PATH_MAX];
    if ( getcwd( currentDir, sizeof( currentDir ) ) != NULL )
    {
        CameraDataSubscriber subscriber;
        DDSDataSourceConfig config = { 1,
                                       SensorSourceType::CAMERA,
                                       0,
                                       "",
                                       "testTopic",
                                       "TOPIC_QOS_DEFAULT",
                                       "testReader",
                                       "",
                                       std::string( currentDir ) + "/",
                                       DDSTransportType::UDP

        };
        ASSERT_TRUE( subscriber.init( config ) );

        ASSERT_TRUE( subscriber.connect() );
        ASSERT_FALSE( subscriber.isAlive() );
        localSensorDataListener dataListener;
        subscriber.subscribeListener( &dataListener );
        TestPublisher publisher;
        ASSERT_TRUE( publisher.init( DDSTransportType::UDP ) );
        // Give some time so that the DDS Stack gets loaded and the multicast gets agreed.
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        ASSERT_TRUE( subscriber.isAlive() );
        // Send one message
        publisher.publishTestDataWithRealPNG( "testPersitUDP.png",
                                              std::string( currentDir ) + "/CameraSubscriberTestPNG.png" );
        // Give some time for the worker to wake up and consume the message.
        std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
        ASSERT_TRUE( dataListener.gotNotification );
        ASSERT_EQ( dataListener.artifact.path, config.temporaryCacheLocation + "testPersitUDP.png" );
        // Verify that  what we send on DDS is exactly what we received i.e the same PNG file.
        ASSERT_TRUE(
            areIdentical( std::string( currentDir ) + "/CameraSubscriberTestPNG.png", dataListener.artifact.path ) );
        ASSERT_TRUE( subscriber.disconnect() );
        ASSERT_FALSE( subscriber.isAlive() );
    }
    else
    {
        std::cout << "Could not find current working directory" << std::endl;
    }
}

/**
 * @brief Validate the data retrieval ( based on UDP DDS Transport)
 * of a CameraDataItem over DDS using a mocked real publisher. This test validates that
 * the system can support more than image message coming over DDS
 *  1- Create a default source config
 *  2- Assert that the Subscriber is initialized
 *  3- Register a listener to the subscriber to get notified when it has received data.
 *  4- Create a mocked publisher and attach it to the same topic.
 *  5- Validate that the subscriber detected this publisher via an isAlive check.
 *  6- Publish a message of the Topic. The message consists of a PNG file.
 *  7- Assert that the Subscriber received the message
 *  8- Assert that the Subscriber propagated the data to the listener
 *  9- Check that the PNG data send is exactly the same received.
 *  9- Disconnect the subscriber
 */
TEST( CameraDataSubscriberTest, testReceiveDataAndPersistUDPMultipleImages )
{
    char currentDir[PATH_MAX];
    if ( getcwd( currentDir, sizeof( currentDir ) ) != NULL )
    {
        CameraDataSubscriber subscriber;
        DDSDataSourceConfig config = { 1,
                                       SensorSourceType::CAMERA,
                                       0,
                                       "",
                                       "testTopic",
                                       "TOPIC_QOS_DEFAULT",
                                       "testReader",
                                       "",
                                       std::string( currentDir ) + "/",
                                       DDSTransportType::UDP

        };
        ASSERT_TRUE( subscriber.init( config ) );

        ASSERT_TRUE( subscriber.connect() );
        ASSERT_FALSE( subscriber.isAlive() );
        localSensorDataListener dataListener;
        subscriber.subscribeListener( &dataListener );
        TestPublisher publisher;
        ASSERT_TRUE( publisher.init( DDSTransportType::UDP ) );
        // Give some time so that the DDS Stack gets loaded and the multicast gets agreed.
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        ASSERT_TRUE( subscriber.isAlive() );
        // Send the first message
        publisher.publishTestDataWithRealPNG( "Image1.png",
                                              std::string( currentDir ) + "/CameraSubscriberTestPNG.png" );
        // Give some time for the worker to wake up and consume the message.

        WAIT_ASSERT_TRUE( dataListener.gotNotification );
        ASSERT_EQ( dataListener.artifact.path, config.temporaryCacheLocation + "Image1.png" );
        // Verify that  what we send on DDS is exactly what we received i.e the same PNG file.
        ASSERT_TRUE(
            areIdentical( std::string( currentDir ) + "/CameraSubscriberTestPNG.png", dataListener.artifact.path ) );

        // Send the second message
        publisher.publishTestDataWithRealPNG( "Image2.png",
                                              std::string( currentDir ) + "/CameraSubscriberTestPNG.png" );

        // Give some time for the worker to wake up and consume the message.

        WAIT_ASSERT_TRUE( dataListener.gotNotification );
        WAIT_ASSERT_EQ( dataListener.artifact.path, config.temporaryCacheLocation + "Image2.png" );
        // Verify that  what we send on DDS is exactly what we received i.e the same PNG file.
        ASSERT_TRUE(
            areIdentical( std::string( currentDir ) + "/CameraSubscriberTestPNG.png", dataListener.artifact.path ) );
        ASSERT_TRUE( subscriber.disconnect() );
        ASSERT_FALSE( subscriber.isAlive() );
    }
    else
    {
        std::cout << "Could not find current working directory" << std::endl;
    }
}
