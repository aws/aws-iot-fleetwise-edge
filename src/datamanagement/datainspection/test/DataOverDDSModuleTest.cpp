
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataOverDDSModule.h"
#include "CollectionInspectionEngine.h"
#include "Testing.h"
#include "WaitUntil.h"
#include "dds/CameraDataPublisher.h"
#include "dds/CameraDataSubscriber.h"
#include "dds/DDSDataTypes.h"
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::TestingSupport;

class TestDDSModule : public DataOverDDSModule
{

public:
    TestDDSModule()
        : receivedArtifact( false )
    {
    }

    ~TestDDSModule()
    {
    }
    void
    onSensorArtifactAvailable( const SensorArtifactMetadata &artifactMetadata ) override
    {
        receivedArtifact = true;
        artifact = artifactMetadata;
    }

public:
    bool receivedArtifact;
    SensorArtifactMetadata artifact;
};

class TestDDSModuleInspection : public DataOverDDSModule
{

public:
    TestDDSModuleInspection()
        : receivedEvent( false )
    {
    }

    ~TestDDSModuleInspection()
    {
    }

    void
    onEventOfInterestDetected( const std::vector<EventMetadata> &eventMetadata ) override
    {
        receivedEvent = true;
        event = eventMetadata;
    }

public:
    bool receivedEvent;
    std::vector<EventMetadata> event;
};
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
        if ( mTestTopic != nullptr )
        {
            mTestParticipant->delete_topic( mTestTopic );
        }
        if ( mTestSubscriber != nullptr )
        {
            mTestParticipant->delete_subscriber( mTestSubscriber );
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
        mTestTopic = mTestParticipant->create_topic( "testRequestTopic", mTestType.get_type_name(), TOPIC_QOS_DEFAULT );

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
            std::shared_ptr<eprosima::fastdds::rtps::SharedMemTransportDescriptor> shmTransport =
                std::make_shared<eprosima::fastdds::rtps::SharedMemTransportDescriptor>();
            // Link the Transport Layer to the Participant.
            participantQos.transport().user_transports.push_back( shmTransport );
        }

        mTestParticipant = DomainParticipantFactory::get_instance()->create_participant( 0, participantQos );

        if ( mTestParticipant == nullptr )
        {
            return false;
        }

        // Register the Type
        mTestType.register_type( mTestParticipant );

        // Create the publications Topic
        mTestTopic =
            mTestParticipant->create_topic( "testResponseTopic", mTestType.get_type_name(), TOPIC_QOS_DEFAULT );

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

private:
    CameraDataItem mTestItem;

    DomainParticipant *mTestParticipant;

    Publisher *mTestPublisher;

    Topic *mTestTopic;

    DataWriter *mTestWriter;

    TypeSupport mTestType;
};

/** @brief Test to verify that the module would fail to init
 * if a subscriber or a publisher failed to init.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleInitFailure )
{
    // Create one source config with invalid Transport
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testTopic",
                                   "testTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::INVALID_TRANSPORT };
    configList.emplace_back( config );
    DataOverDDSModule testModule;
    ASSERT_FALSE( testModule.init( configList ) );
    // Try with an unsupported device type e.g. RADAR
    configList.clear();
    config = { 1,
               SensorSourceType::RADAR,
               0,
               "testTopic",
               "testTopic",
               "TOPIC_QOS_DEFAULT",
               "testReader",
               "TestWriter",
               "/tmp/camera/test/",
               DDSTransportType::SHM };
    ASSERT_FALSE( testModule.init( configList ) );
}

/** @brief Test to verify that the module would success to init
 * if a subscriber and the publisher both succeeded to do so.
 * The difference here is that we are passing a correct config
 * to the module.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleInitSuccess )
{
    // Create one source config with valid Transport and correct
    // device type.
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    configList.emplace_back( config );
    DataOverDDSModule testModule;
    ASSERT_TRUE( testModule.init( configList ) );
}

/** @brief Test to verify that the module would success to init
 * if a subscriber and the publisher both succeeded to do so, and
 * also connect both to the DDS Network. However, the module will not
 * be alive if the peer nodes are not registered.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleInitAndConnectSuccessButNotAlive )
{
    // Create one source config with valid Transport and correct
    // device type but the remote DDS Node is not available, so
    // the module should not be alive
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    configList.emplace_back( config );
    DataOverDDSModule testModule;
    ASSERT_TRUE( testModule.init( configList ) );
    ASSERT_TRUE( testModule.connect() );
    ASSERT_FALSE( testModule.isAlive() );
    ASSERT_TRUE( testModule.disconnect() );
}

/** @brief Test to verify that the module would succeed to init
 * if a subscriber and the publisher both succeeded to do so, and
 * also connect both to the DDS Network. By registering some peer nodes
 * on the network, the module should be alive and ready to pub/messages.
 * This test case used SHM as a transport.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleInitAndConnectSuccessAndIsAliveSHM )
{
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    configList.emplace_back( config );
    DataOverDDSModule testModule;
    // Create a mock Subscriber node on the request topic
    TestSubscriber testSub;
    ASSERT_TRUE( testSub.init( DDSTransportType::SHM ) );
    // Create a mock Publisher node on the response topic
    TestPublisher testPub;
    ASSERT_TRUE( testPub.init( DDSTransportType::SHM ) );

    // The  module should be now alive
    // Init the module and connect it
    ASSERT_TRUE( testModule.init( configList ) );
    ASSERT_TRUE( testModule.connect() );
    // Give some time till the threads are warmed up

    WAIT_ASSERT_TRUE( testModule.isAlive() );
    ASSERT_TRUE( testModule.disconnect() );
}

/** @brief Test to verify that the module would success to init
 * if a subscriber and the publisher both succeeded to do so, and
 * also connect both to the DDS Network. By registering some peer nodes
 * on the network, the module should be alive and ready to pub/messages.
 * This test case used UDP as a transport.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleInitAndConnectSuccessAndIsAliveUDP )
{
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::UDP };
    configList.emplace_back( config );
    DataOverDDSModule testModule;
    // Create a mock Subscriber node on the request topic
    TestSubscriber testSub;
    ASSERT_TRUE( testSub.init( DDSTransportType::UDP ) );
    // Create a mock Publisher node on the response topic
    TestPublisher testPub;
    ASSERT_TRUE( testPub.init( DDSTransportType::UDP ) );
    // Init the module and connect it
    ASSERT_TRUE( testModule.init( configList ) );
    ASSERT_TRUE( testModule.connect() );
    // Give some time till the threads are warmed up

    // The  module should be now alive
    WAIT_ASSERT_TRUE( testModule.isAlive() );
    ASSERT_TRUE( testModule.disconnect() );
}

/** @brief Test to verify that the module upon a reception of
 * an event from a mocked inspection engine, would react and
 * send a DDS message.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleSendaRequest )
{
    // Create one source config with valid Transport and correct
    // device type but the remote DDS Node is not available, so
    // the module should not be alive
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    configList.emplace_back( config );
    DataOverDDSModule testModule;
    // Create a mock Subscriber node on the request topic
    TestSubscriber testSub;
    ASSERT_TRUE( testSub.init( DDSTransportType::SHM ) );
    // Create a mock Publisher node on the response topic
    TestPublisher testPub;
    ASSERT_TRUE( testPub.init( DDSTransportType::SHM ) );
    // Init the module and connect it
    ASSERT_TRUE( testModule.init( configList ) );
    ASSERT_TRUE( testModule.connect() );
    // Give some time till the threads are warmed up

    WAIT_ASSERT_TRUE( testModule.isAlive() );
    // Create a notification about an event.
    // An event of ID 123 on sourceID 1, with 1 ms as positive and negative offsets
    std::vector<EventMetadata> mockedEvent;
    mockedEvent.emplace_back( 123, 1, 1, 1 );
    // Notify the Module about this event
    testModule.onEventOfInterestDetected( mockedEvent );
    // Wait a bit till the thread wakes up and sends the request over DDS to the Subscriber

    // Check that the same event has been propagated to the peer DDS Node
    WAIT_ASSERT_EQ( testSub.dataItem.dataItemId(), 123U );
    ASSERT_EQ( testSub.dataItem.negativeOffsetMs(), 1 );
    ASSERT_EQ( testSub.dataItem.positiveOffsetMs(), 1 );
    ASSERT_TRUE( testModule.disconnect() );
}

/** @brief Test to verify that the module upon a reception of
 * an event from a mocked inspection engine, would react and
 * send a DDS message. It will also receive a response from the DDS Node.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleSendaRequestReceiveResponse )
{
    // Create one source config with valid Transport and correct
    // device type but the remote DDS Node is not available, so
    // the module should not be alive
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/",
                                   DDSTransportType::SHM };
    configList.emplace_back( config );
    TestDDSModule testModule;
    // Create a mock Subscriber node on the request topic
    TestSubscriber testSub;
    ASSERT_TRUE( testSub.init( DDSTransportType::SHM ) );
    // Create a mock Publisher node on the response topic
    TestPublisher testPub;
    ASSERT_TRUE( testPub.init( DDSTransportType::SHM ) );
    // Init the module and connect it
    ASSERT_TRUE( testModule.init( configList ) );
    ASSERT_TRUE( testModule.connect() );
    // Give some time till the threads are warmed up

    // The  module should be now alive
    WAIT_ASSERT_TRUE( testModule.isAlive() );
    // Create a notification about an event.
    // An event of ID 123 on sourceID 1, with 1 ms as positive and negative offsets
    std::vector<EventMetadata> mockedEvent;
    mockedEvent.emplace_back( 123, 1, 1, 1 );
    // Notify the Module about this event
    testModule.onEventOfInterestDetected( mockedEvent );
    // Wait a bit till the thread wakes up and sends the request over DDS to the Subscriber

    // Check that the same event has been propagated to the peer DDS Node
    WAIT_ASSERT_EQ( testSub.dataItem.dataItemId(), 123U );
    ASSERT_EQ( testSub.dataItem.negativeOffsetMs(), 1 );
    ASSERT_EQ( testSub.dataItem.positiveOffsetMs(), 1 );
    // Send a response from the DDS Node back.
    testPub.publishTestData( "123" );
    // Give it some time till the artifact is received

    WAIT_ASSERT_TRUE( testModule.receivedArtifact );
    WAIT_ASSERT_EQ( testModule.artifact.sourceID, config.sourceID );
    ASSERT_EQ( testModule.artifact.path, config.temporaryCacheLocation + "123" );
    ASSERT_TRUE( testModule.disconnect() );
}

/** @brief Test to validate that if a collectionScheme is set on the inspection
 * engine that includes a conditition requiring Image capture,
 * the DDS Module is informed when the condition is met.
 */
TEST( DataOverDDSModuleTest, DataOverDDSModuleReceiveNotificationFromInspectionEngine )
{
    // Create one source config with valid Transport and correct
    // device type but the remote DDS Node is not available, so
    // the module should not be alive
    DDSDataSourcesConfig configList;
    DDSDataSourceConfig config = { 1,
                                   SensorSourceType::CAMERA,
                                   0,
                                   "testRequestTopic",
                                   "testResponseTopic",
                                   "TOPIC_QOS_DEFAULT",
                                   "testReader",
                                   "TestWriter",
                                   "/tmp/camera/test/",
                                   DDSTransportType::SHM };
    configList.emplace_back( config );
    // Init the module and connect it
    TestDDSModuleInspection testModule;
    ASSERT_TRUE( testModule.init( configList ) );
    ASSERT_TRUE( testModule.connect() );

    // Prepare the Inspection Engine and the inspection matrix
    InspectionMatrix matrix;
    InspectionMatrixSignalCollectionInfo matrixCollectInfo;
    ConditionWithCollectedData condition;
    matrixCollectInfo.signalID = 1234;
    matrixCollectInfo.sampleBufferSize = 50;
    matrixCollectInfo.minimumSampleIntervalMs = 10;
    matrixCollectInfo.fixedWindowPeriod = 77777;
    matrixCollectInfo.isConditionOnlySignal = true;
    condition.signals.push_back( matrixCollectInfo );
    condition.afterDuration = 3;
    condition.minimumPublishIntervalMs = 0;
    condition.includeImageCapture = true;
    // Create the Image data capture setting with 1 device
    InspectionMatrixImageCollectionInfo imageCollectionInfoItem = {
        config.sourceID, 0, InspectionMatrixImageCollectionType::TIME_BASED, 3 };
    condition.imageCollectionInfos.emplace_back( imageCollectionInfoItem );
    condition.probabilityToSend = 1.0;
    condition.includeActiveDtcs = false;
    condition.triggerOnlyOnRisingEdge = false;
    // Node
    ExpressionNode node;
    node.nodeType = ExpressionNodeType::BOOLEAN;
    node.booleanValue = true;
    condition.condition = &node;
    // add the condition to the matrix
    matrix.conditions.emplace_back( condition );
    // Engine
    CollectionInspectionEngine engine;
    // Register the DDS Module as a listener so to receive notifications
    ASSERT_TRUE( engine.subscribeListener( &testModule ) );
    // Set the matrix
    engine.onChangeInspectionMatrix( std::make_shared<InspectionMatrix>( matrix ) );
    // Start the inspection
    TimePoint timestamp = { 160000000, 100 };
    uint32_t waitTimeMs = 0;
    ASSERT_TRUE( engine.evaluateConditions( timestamp ) );
    // Wait for the afterDuration before checking
    timestamp += condition.afterDuration;
    engine.collectNextDataToSend( timestamp, waitTimeMs );
    // Since the condition has to trigger image collection
    // we must have received a notification
    ASSERT_TRUE( testModule.receivedEvent );
    // One event of size one should have been received
    WAIT_ASSERT_EQ( testModule.event.size(), 1U );
    ASSERT_EQ( testModule.event[0].sourceID, config.sourceID );
    // we need a total of condition.afterDuration + condition.imageCollectionInfo.beforeDurationMs
    // from the camera module. As we have yet to consolidate the Inspection Engine evaluate and
    // collectNextData, we request eventually the whole time but as a negative offset.
    // refer to evaluateAndTriggerRichSensorCapture in CollectionInspectionEngine
    // for further info.
    ASSERT_EQ( testModule.event[0].positiveOffsetMs, 0 );
    ASSERT_EQ( testModule.event[0].negativeOffsetMs,
               condition.imageCollectionInfos[0].beforeDurationMs + condition.afterDuration );
    ASSERT_TRUE( testModule.disconnect() );
    engine.unSubscribeListener( &testModule );
}
