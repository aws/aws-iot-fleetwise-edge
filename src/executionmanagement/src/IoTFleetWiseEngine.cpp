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
#include "IoTFleetWiseEngine.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionSchemeJSONParser.h"
#include "NetworkChannelConsumer.h"
#include "TraceModule.h"
#include "businterfaces/SocketCANBusChannel.h"
#include <boost/lockfree/spsc_queue.hpp>

namespace Aws
{
namespace IoTFleetWise
{
namespace ExecutionManagement
{
using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::Platform::PersistencyManagement;
using Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams;
using Aws::IoTFleetWise::OffboardConnectivity::ConnectivityError;

IoTFleetWiseEngine::IoTFleetWiseEngine()
    : mAwsIotChannelMetricsUpload( nullptr )
    , mAwsIotChannelLogsUpload( nullptr )
{
    TraceModule::get().sectionBegin( FWE_STARTUP );
}

IoTFleetWiseEngine::~IoTFleetWiseEngine()
{
    if ( mCollectionScheme )
    {
        mCollectionScheme->clear();
    }
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
    setLogForwarding( nullptr );
}

bool
IoTFleetWiseEngine::connect( const Json::Value &config )
{
    // Main bootstrap sequence.
    try
    {
        /*************************Payload Manager and Persistency library bootstrap begin*********/

        // Create an object for Persistency
        mPersistDecoderManifestCollectionSchemesAndData = std::make_shared<CacheAndPersist>(
            config["staticConfig"]["persistency"]["persistencyPath"].asString(),
            config["staticConfig"]["persistency"]["persistencyPartitionMaxSize"].asInt() );
        if ( !mPersistDecoderManifestCollectionSchemesAndData->init() )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to init persistency library " );
        }

        // Payload Manager for offline data management
        mPayloadManager = std::make_shared<PayloadManager>( mPersistDecoderManifestCollectionSchemesAndData );

        /*************************Payload Manager and Persistency library bootstrap end************/

        /*************************CAN InterfaceID to InternalID Translator begin*********/
        CANInterfaceIDTranslator canIDTranslator;

        // Initialize
        for ( const auto &interfaceName : config["networkInterfaces"] )
        {
            if ( interfaceName["type"].asString() == "canInterface" )
            {
                canIDTranslator.add( interfaceName["interfaceId"].asString() );
            }
        }
        /*************************CAN InterfaceID to InternalID Translator end*********/

        /**************************Connectivity bootstrap begin*******************************/

        mAwsIotModule = std::make_shared<AwsIotConnectivityModule>();
        mAwsIotModule->connect( config["staticConfig"]["mqttConnection"]["privateKeyFilename"].asString(),
                                config["staticConfig"]["mqttConnection"]["certificateFilename"].asString(),
                                config["staticConfig"]["mqttConnection"]["endpointUrl"].asString(),
                                config["staticConfig"]["mqttConnection"]["clientId"].asString(),
                                true );

        // Only CAN data channel needs a payloadManager object for persistency and compression support,
        // for other components this will be nullptr
        mAwsIotChannelSendCanData = mAwsIotModule->createNewChannel( mPayloadManager );
        mAwsIotChannelSendCanData->setTopic( config["staticConfig"]["mqttConnection"]["canDataTopic"].asString() );

        mAwsIotChannelReceiveCollectionSchemeList = mAwsIotModule->createNewChannel( nullptr );
        mAwsIotChannelReceiveCollectionSchemeList->setTopic(
            config["staticConfig"]["mqttConnection"]["collectionSchemeListTopic"].asString(), true );

        mAwsIotChannelReceiveDecoderManifest = mAwsIotModule->createNewChannel( nullptr );
        mAwsIotChannelReceiveDecoderManifest->setTopic(
            config["staticConfig"]["mqttConnection"]["decoderManifestTopic"].asString(), true );

        /*
         * Over this channel metrics like performance (resident ram pages, cpu time spent in threads)
         * and tracing metrics like internal variables and time spent in specially instrumented functions
         * spent are uploaded in json format over mqtt.
         */
        if ( config["staticConfig"]["mqttConnection"]["metricsUploadTopic"].asString().length() > 0 )
        {
            mAwsIotChannelMetricsUpload = mAwsIotModule->createNewChannel( nullptr );
            mAwsIotChannelMetricsUpload->setTopic(
                config["staticConfig"]["mqttConnection"]["metricsUploadTopic"].asString() );
        }
        /*
         * Over this channel log messages that are currently logged to STDOUT are uploaded in json
         * format over MQTT.
         */
        if ( config["staticConfig"]["mqttConnection"]["loggingUploadTopic"].asString().length() > 0 )
        {
            mAwsIotChannelLogsUpload = mAwsIotModule->createNewChannel( nullptr );
            mAwsIotChannelLogsUpload->setTopic(
                config["staticConfig"]["mqttConnection"]["loggingUploadTopic"].asString() );
        }

        // Create an ISender for sending Checkins
        mAwsIotChannelSendCheckin = mAwsIotModule->createNewChannel( nullptr );
        mAwsIotChannelSendCheckin->setTopic( config["staticConfig"]["mqttConnection"]["checkinTopic"].asString() );

        // These parameters need to be added to the Config file to enable the feature :
        // useJsonBasedCollectionScheme
        mDataCollectionSender = std::make_shared<DataCollectionSender>(
            mAwsIotChannelSendCanData,
            config["staticConfig"]["internalParameters"]["useJsonBasedCollectionScheme"].asBool(),
            config["staticConfig"]["publishToCloudParameters"]["maxPublishMessageCount"].asUInt(),
            canIDTranslator );

        /*************************Connectivity bootstrap end***************************************/

        /*************************Remote Profiling bootstrap begin**********************************/
        if ( config["staticConfig"].isMember( "remoteProfilerDefaultValues" ) )
        {
            LogLevel logThreshold = LogLevel::Off;
            /*
             * logging-upload-level-threshold specifies which log messages normally output to STDOUT are also
             * uploaded over MQTT. Default is OFF which means no messages are uploaded. If its for example
             * "Warning" all log messages with this or a higher log level are mirrored over MQTT
             */
            stringToLogLevel(
                config["staticConfig"]["remoteProfilerDefaultValues"]["loggingUploadLevelThreshold"].asString(),
                logThreshold );

            /*
             * metrics-upload-interval-ms defines the interval in which all metrics should be uploaded
             * 0 means metrics upload is disabled which should be the default. Currently the metrics are
             * uploaded every given interval independent if the value changed or not
             *
             * logging-upload-max-wait-before-upload-ms to avoid to many separate mqtt messages the log messages
             * are aggregated and sent out delayed. The maximum allowed delay is specified here
             *
             * profiler-prefix metrics names uploaded will be prefixed with this string which could
             * be set different for every vehicle
             */

            // These parameters need to be added to the Config file to enable the feature :
            // metricsUploadIntervalMs
            // loggingUploadMaxWaitBeforeUploadMs
            // profilerPrefix
            mRemoteProfiler = std::unique_ptr<RemoteProfiler>( new RemoteProfiler(
                mAwsIotChannelMetricsUpload,
                mAwsIotChannelLogsUpload,
                config["staticConfig"]["remoteProfilerDefaultValues"]["metricsUploadIntervalMs"].asUInt(),
                config["staticConfig"]["remoteProfilerDefaultValues"]["loggingUploadMaxWaitBeforeUploadMs"].asUInt(),
                logThreshold,
                config["staticConfig"]["remoteProfilerDefaultValues"]["profilerPrefix"].asString() ) );
            if ( !mRemoteProfiler->start() )
            {
                mLogger.warn(
                    "IoTFleetWiseEngine::connect",
                    " Failed to start the Remote Profiler - No remote profiling available until FWE restart " );
            }
            setLogForwarding( mRemoteProfiler.get() );
        }
        /*************************Remote Profiling bootstrap ends**********************************/

        /*************************Inspection Engine bootstrap begin*********************************/

        // Below are three buffers to be shared between Network Channel Consumer and Collection Engine
        // Signal Buffer are a lock-free multi-producer single consumer buffer
        auto signalBufferPtr =
            std::make_shared<SignalBuffer>( config["staticConfig"]["bufferSizes"]["decodedSignalsBufferSize"].asInt() );
        // CAN Buffer are a lock-free multi-producer single consumer buffer
        auto canRawBufferPtr =
            std::make_shared<CANBuffer>( config["staticConfig"]["bufferSizes"]["rawCANFrameBufferSize"].asInt() );
        // DTC Buffer are a single producer single consumer buffer
        auto activeDTCBufferPtr =
            std::make_shared<ActiveDTCBuffer>( config["staticConfig"]["bufferSizes"]["dtcBufferSize"].asInt() );
        // Create the Data Inspection Queue
        mCollectedDataReadyToPublish = std::make_shared<CollectedDataReadyToPublish>(
            config["staticConfig"]["internalParameters"]["readyToPublishDataBufferSize"].asInt() );

        // Init and start the Inspection Engine
        mCollectionInspectionWorkerThread = std::make_shared<CollectionInspectionWorkerThread>();
        if ( !mCollectionInspectionWorkerThread->init(
                 signalBufferPtr,
                 canRawBufferPtr,
                 activeDTCBufferPtr,
                 mCollectedDataReadyToPublish,
                 config["staticConfig"]["threadIdleTimes"]["inspectionThreadIdleTimeMs"].asUInt(),
                 config["staticConfig"]["internalParameters"]["dataReductionProbabilityDisabled"].asBool() ) ||
             !mCollectionInspectionWorkerThread->start() )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to init and start the Inspection Engine " );
            return false;
        }
        // Make sure the Inspection Engine can notify the Bootstrap thread about ready to be
        // published data.
        if ( !mCollectionInspectionWorkerThread->subscribeListener( this ) )
        {
            mLogger.error( "IoTFleetWiseEngine::connect",
                           " Failed register the Engine Thread to the Inspection Module " );
            return false;
        }

        /*************************Inspection Engine bootstrap end***********************************/

        /*************************CollectionScheme Insgestion bootstrap begin*********************************/

        // These parameters need to be added to the Config file to enable the feature :
        // jsonBasedCollectionSchemeFilename
        // JsonBasedCollectionScheme
        if ( config["staticConfig"]["internalParameters"]["useJsonBasedCollectionScheme"].asBool() )
        {
            // The main purpose to keep this code is for fast testing by enabling json collectionScheme as a file
            CollectionSchemeJSONParser parser(
                config["staticConfig"]["internalParameters"]["jsonBasedCollectionSchemeFilename"].asString() );

            if ( !parser.parse() || !parser.getCollectionScheme() || !parser.getCollectionScheme()->isValid() )
            {
                mLogger.error( "IoTFleetWiseEngine::connect", " Failed to Parse the Collection Scheme " );
                return false;
            }

            mCollectionScheme = parser.getCollectionScheme();
        }
        else
        {
            // CollectionScheme Ingestion module executes in the context for the offboardconnectivity thread. Upcoming
            // messages are expected to come either on the decoder manifest topic or the collectionScheme topic or both
            // ( eventually ).
            mSchemaPtr = std::make_shared<Schema>( mAwsIotChannelReceiveDecoderManifest,
                                                   mAwsIotChannelReceiveCollectionSchemeList,
                                                   mAwsIotChannelSendCheckin );
        }

        /*****************************CollectionScheme Management bootstrap begin*****************************/

        // Create and connect the CollectionScheme Manager
        mCollectionSchemeManagerPtr = std::make_shared<CollectionSchemeManager>();

        if ( !mCollectionSchemeManagerPtr->init(
                 config["staticConfig"]["publishToCloudParameters"]["collectionSchemeManagementCheckinIntervalMs"]
                     .asUInt(),
                 mPersistDecoderManifestCollectionSchemesAndData,
                 canIDTranslator ) )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to init the CollectionScheme Manager " );
            return false;
        }

        // Make sure the CollectionScheme Ingestion can notify the CollectionScheme Manager about the arrival
        // of new artifacts over the offboardconnectivity channel.
        if ( !mSchemaPtr->subscribeListener(
                 static_cast<CollectionSchemeManagementListener *>( mCollectionSchemeManagerPtr.get() ) ) )
        {
            mLogger.error( "IoTFleetWiseEngine::connect",
                           " Failed register the CollectionScheme Manager to the CollectionScheme Ingestion Module " );
            return false;
        }

        // Make sure the CollectionScheme Manager can notify the Inspection Engine about the availability of
        // a new set of collection CollectionSchemes.
        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                 static_cast<IActiveConditionProcessor *>( mCollectionInspectionWorkerThread.get() ) ) )
        {
            mLogger.error( "IoTFleetWiseEngine::connect",
                           " Failed register the Inspection Engine to the CollectionScheme Manager Module " );
            return false;
        }

        // Allow CollectionSchemeManagement to send checkins through the Schema Object Callback
        mCollectionSchemeManagerPtr->setSchemaListenerPtr( mSchemaPtr );

        /********************************Network Binder bootstrap start*******************************/

        // Initialize the Network Management layer.
        // Start with the Binder, then the Network Channels and Consumers.
        mBinder.reset( new NetworkChannelBinder() );
        if ( mBinder.get() == nullptr || !mBinder->connect() )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to initialize the network binder " );
            return false;
        }

        auto obdOverCANModuleInit = false;

        // Initialize
        for ( const auto &interfaceName : config["networkInterfaces"] )
        {
            if ( interfaceName["type"].asString() == "canInterface" )
            {
                std::shared_ptr<INetworkChannelBridge> channelPtr;
                channelPtr.reset(
                    new SocketCANBusChannel( interfaceName["canInterface"]["interfaceName"].asString() ) );
                std::shared_ptr<INetworkChannelConsumer> consumerPtr;
                consumerPtr.reset( new NetworkChannelConsumer() );
                if ( channelPtr.get() == nullptr || consumerPtr.get() == nullptr )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to create consumer/producer " );
                    return false;
                }
                // Initialize the consumer/producers
                // Currently we limit 1 channel to a single consumer. We can always extend this
                // if we want to process the data coming from 1 channel to multiple consumers.
                if ( !channelPtr->init(
                         config["staticConfig"]["bufferSizes"]["socketCANBufferSize"].asUInt(),
                         config["staticConfig"]["threadIdleTimes"]["socketCANThreadIdleTimeMs"].asUInt() ) ||
                     !consumerPtr->init(
                         static_cast<uint8_t>(
                             canIDTranslator.getChannelNumericID( interfaceName["interfaceId"].asString() ) ),
                         signalBufferPtr,
                         canRawBufferPtr,
                         config["staticConfig"]["threadIdleTimes"]["canDecoderThreadIdleTimeMs"].asUInt() ) )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to initialize the producers/consumers " );
                    return false;
                }

                // Handshake the binder and the channel
                if ( !mBinder->addNetworkChannel( channelPtr ) )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to add a network channel " );
                    return false;
                }

                if ( !mBinder->bindConsumerToNetworkChannel( consumerPtr, channelPtr->getChannelID() ) )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to Bind Consumers to Producers " );
                    return false;
                }
            }
            else if ( interfaceName["type"].asString() == "obdInterface" )
            {
                if ( !obdOverCANModuleInit )
                {
                    auto obdOverCANModule = std::make_shared<OBDOverCANModule>();
                    obdOverCANModuleInit = true;
                    // Init returns false if no collection is configured:
                    if ( obdOverCANModule->init( signalBufferPtr,
                                                 activeDTCBufferPtr,
                                                 interfaceName["obdInterface"]["interfaceName"].asString(),
                                                 interfaceName["obdInterface"]["pidRequestIntervalSeconds"].asUInt(),
                                                 interfaceName["obdInterface"]["dtcRequestIntervalSeconds"].asUInt(),
                                                 interfaceName["obdInterface"]["useExtendedIds"].asBool(),
                                                 interfaceName["obdInterface"]["hasTransmissionEcu"].asBool() ) )
                    {
                        // Connect the OBD Module
                        mOBDOverCANModule = obdOverCANModule;
                        if ( !mOBDOverCANModule->connect() )
                        {
                            mLogger.error( "IoTFleetWiseEngine::connect", "Failed to connect OBD over CAN module" );
                            return false;
                        }

                        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                                 static_cast<IActiveDecoderDictionaryListener *>( mOBDOverCANModule.get() ) ) )
                        {
                            mLogger.error( "IoTFleetWiseEngine::connect",
                                           " Failed to register the OBD Module to the CollectionScheme Manager" );
                            return false;
                        }
                        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                                 static_cast<IActiveConditionProcessor *>( mOBDOverCANModule.get() ) ) )
                        {
                            mLogger.error( "IoTFleetWiseEngine::connect",
                                           " Failed to register the OBD Module to the CollectionScheme Manager" );
                            return false;
                        }
                    }
                }
                else
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", "obdOverCANModule already initialised" );
                }
            }
            else
            {
                mLogger.error( "IoTFleetWiseEngine::connect", interfaceName["type"].asString() + " is not supported" );
            }
        }
        // Register Network Channel Binder as listener for CollectionScheme Manager
        mCollectionSchemeManagerPtr->subscribeListener(
            static_cast<IActiveDecoderDictionaryListener *>( mBinder.get() ) );
        /********************************Network Binder bootstrap end*******************************/

        // Only start the CollectionSchemeManager after all listeners have subscribed, otherwise
        // they will not be notified of the initial decoder manifest and collection schemes that are
        // read from persistent memory:
        if ( !mCollectionSchemeManagerPtr->connect() )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to start the CollectionScheme Manager " );
            return false;
        }
        /****************************CollectionScheme Manager bootstrap end*************************/

#ifdef FWE_FEATURE_CAMERA
        /********************************DDS Module bootstrap start*********************************/
        // First we need to parse the configuration
        // and extract all the node(s) settings our DDS module needs. Those are per node( device):
        // - The device unique ID.
        // - The device Type e.g. CAMERA.
        // - The upstream ( Publish ) and downstream( subscribe ) DDS Topics.
        // - The Topics DDS QoS.
        // - The DDS Transport to be used ( SHM or UDP ).
        // - The temporary location where the data artifact received will be stored.
        // - Writer and Reader names that will be registered on the DDS Network.
        // - The DDS Domain ID that we should register too.
        // Other configurations can be added as per the need e.g. UDP Ports/IPs

        DDSDataSourcesConfig ddsNodes;
        for ( const auto &ddsNode : config["dds-nodes-configuration"] )
        {
            DDSDataSourceConfig nodeConfig;
            // Transport, currently only UDP and SHM are supported
            if ( ddsNode["dds-transport-protocol"].asString() == "SHM" )
            {
                nodeConfig.transportType = DDSTransportType::SHM;
            }
            else if ( ddsNode["dds-transport-protocol"].asString() == "UDP" )
            {
                nodeConfig.transportType = DDSTransportType::UDP;
            }
            else
            {
                mLogger.warn( "IoTFleetWiseEngine::connect",
                              " Unsupported Transport config provided for a DDS Node, skipping it" );
                continue;
            }

            // Device Type, currently only CAMERA is supported
            if ( ddsNode["dds-device-type"].asString() == "CAMERA" )
            {
                nodeConfig.sourceType = SensorSourceType::CAMERA;
            }
            else
            {
                mLogger.warn( "IoTFleetWiseEngine::connect",
                              " Unsupported Device type provided for a DDS Node, skipping it" );
                continue;
            }

            nodeConfig.sourceID = ddsNode["dds-device-id"].asUInt();
            nodeConfig.domainID = ddsNode["dds-domain-id"].asUInt();
            nodeConfig.readerName = ddsNode["dds-reader-name"].asString();
            nodeConfig.writerName = ddsNode["dds-writer-name"].asString();
            nodeConfig.publishTopicName = ddsNode["upstream-dds-topic-name"].asString();
            nodeConfig.subscribeTopicName = ddsNode["downstream-dds-topic-name"].asString();
            nodeConfig.topicQoS = ddsNode["dds-topics-qos"].asString();
            nodeConfig.temporaryCacheLocation = ddsNode["dds-tmp-cache-location"].asString();
            ddsNodes.emplace_back( nodeConfig );
        }
        // Only if there is at least one DDS Node, we should create the DDS Module
        if ( !ddsNodes.empty() )
        {
            mDataOverDDSModule.reset( new DataOverDDSModule() );
            // Init the Module
            if ( mDataOverDDSModule.get() == nullptr || !mDataOverDDSModule->init( ddsNodes ) )
            {
                mLogger.error( "IoTFleetWiseEngine::connect", " Failed to initialize the DDS Module " );
                return false;
            }
            // Register the DDS Module as a listener to the Inspection Engine and connect it.
            if ( !mCollectionInspectionWorkerThread->subscribeToEvents(
                     static_cast<InspectionEventListener *>( mDataOverDDSModule.get() ) ) ||
                 !mDataOverDDSModule->connect() )
            {
                mLogger.error( "IoTFleetWiseEngine::connect", " Failed to connect the DDS Module " );
                return false;
            }
            mLogger.info( "IoTFleetWiseEngine::connect", " DDS Module connected " );
        }
        else
        {
            mLogger.info( "IoTFleetWiseEngine::connect", " DDS Module disabled   " );
        }

        /********************************DDS Module bootstrap end*********************************/
#endif // FWE_FEATURE_CAMERA
    }
    catch ( const std::exception &e )
    {
        mLogger.error( "IoTFleetWiseEngine::connect",
                       "Fatal Error during AWS IoT FleetWise Bootstrap:" + std::string( e.what() ) );
        return false;
    }

    mLogger.info( "IoTFleetWiseEngine::connect", "Engine Connected" );

    return true;
}

bool
IoTFleetWiseEngine::disconnect()
{
#ifdef FWE_FEATURE_CAMERA
    if ( mDataOverDDSModule )
    {
        if ( !mCollectionInspectionWorkerThread->unSubscribeFromEvents(
                 static_cast<InspectionEventListener *>( mDataOverDDSModule.get() ) ) ||
             !mDataOverDDSModule->disconnect() )
        {

            mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not disconnect DDS Module" );
            return false;
        }
    }
#endif // FWE_FEATURE_CAMERA

    if ( mOBDOverCANModule )
    {
        if ( !mOBDOverCANModule->disconnect() )
        {
            mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not disconnect OBD over CAN module" );
            return false;
        }
    }

    if ( !mCollectionInspectionWorkerThread->stop() )
    {
        mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not stop the Inspection Engine" );
        return false;
    }

    setLogForwarding( nullptr );
    if ( mRemoteProfiler != nullptr && !mRemoteProfiler->stop() )
    {
        mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not stop the Remote Profiler" );
        return false;
    }

    if ( !mCollectionSchemeManagerPtr->disconnect() )
    {
        mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not stop the CollectionScheme Manager" );
        return false;
    }

    // Stop the Binder
    if ( !mBinder->disconnect() )
    {
        mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not disconnect the Binder" );
        return false;
    }
    mLogger.info( "IoTFleetWiseEngine::disconnect", "Engine Disconnected" );
    TraceModule::get().sectionEnd( FWE_SHUTDOWN );
    TraceModule::get().print();

    return true;
}

bool
IoTFleetWiseEngine::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "IoTFleetWiseEngine::start", " Engine Thread failed to start " );
    }
    else
    {
        mLogger.trace( "IoTFleetWiseEngine::start", " Engine Thread started " );
        mThread.setThreadName( "fwEMEngine" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
IoTFleetWiseEngine::stop()
{
    TraceModule::get().sectionBegin( FWE_SHUTDOWN );
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
IoTFleetWiseEngine::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
IoTFleetWiseEngine::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

void
IoTFleetWiseEngine::doWork( void *data )
{

    IoTFleetWiseEngine *engine = static_cast<IoTFleetWiseEngine *>( data );
    // Time in seconds
    double timeTrigger = 0;
    TraceModule::get().sectionEnd( FWE_STARTUP );

    // Check if data was persisted, Retrieve all the data and send
    engine->checkAndSendRetrievedData();

    while ( !engine->shouldStop() )
    {
        // The interrupt arrives because of 2 reasons :
        // 1- An event trigger( either cyclic or interrupt ) has arrived, for this, all the buffers of the consumers
        // should be read and flushed.
        // 2- A consumer has been either reconnected or disconnected on runtime.
        // we should then either register or unregister for callbacks from this consumer.
        // 3- A new collectionScheme is available - First old data is sent out then the new collectionScheme is applied
        // to all consumers of the channel data

        engine->mTimer.reset();
        uint32_t elapsedTimeUs = 0;
        if ( timeTrigger > 0 )
        {
            engine->mLogger.trace( "IoTFleetWiseEngine::doWork",
                                   "Waiting for :" + std::to_string( timeTrigger ) + "seconds" );
            engine->mWait.wait( static_cast<uint32_t>( timeTrigger * 1000 ) );
        }
        else
        {
            engine->mWait.wait( Platform::Signal::WaitWithPredicate );
            elapsedTimeUs += static_cast<uint32_t>( engine->mTimer.getElapsedMs().count() );
            engine->mLogger.trace( "IoTFleetWiseEngine::doWork", "Event Arrived" );
            engine->mLogger.trace( "IoTFleetWiseEngine::doWork",
                                   "Time Elapsed waiting for the Event : " + std::to_string( elapsedTimeUs ) );
        }

        // Dequeues the collected data queue and sends the data to cloud
        auto consumedElements = engine->mCollectedDataReadyToPublish->consume_all(
            [&]( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr ) {
                // Only used for trace logging
                std::string firstSignalValues = "[";
                uint32_t signalPrintCounter = 0;
                for ( auto &s : triggeredCollectionSchemeDataPtr->signals )
                {
                    signalPrintCounter++;
                    if ( signalPrintCounter > MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG )
                    {
                        firstSignalValues += " ...";
                        break;
                    }
                    firstSignalValues += std::to_string( s.signalID ) + ":" + std::to_string( s.value ) + ",";
                }
                firstSignalValues += "]";
                engine->mLogger.info(
                    "IoTFleetWiseEngine::doWork",
                    "FWE data ready to send: Signals:" +
                        std::to_string( triggeredCollectionSchemeDataPtr->signals.size() ) + " " + firstSignalValues +
                        " raw CAN frames:" + std::to_string( triggeredCollectionSchemeDataPtr->canFrames.size() ) +
                        " DTCs:" + std::to_string( triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.size() ) +
                        " Geohash:" + triggeredCollectionSchemeDataPtr->mGeohashInfo.mGeohashString );

                engine->mDataCollectionSender->send( triggeredCollectionSchemeDataPtr );
            } );
        TraceModule::get().setVariable( QUEUE_INSPECTION_TO_SENDER, consumedElements );
    }
}

void
IoTFleetWiseEngine::onDataReadyToPublish()
{
    mWait.notify();
}

void
IoTFleetWiseEngine::checkAndSendRetrievedData()
{
    std::vector<std::string> payloads;

    // Retrieve the data from persistency library
    ErrorCode status = mPayloadManager->retrieveData( payloads );

    if ( status == SUCCESS )
    {
        ConnectivityError res = ConnectivityError::Success;
        mLogger.info( "IoTFleetWiseEngine::checkAndSendRetrievedData",
                      "Number of Payloads to transmit : " + std::to_string( payloads.size() ) );

        for ( const auto &payload : payloads )
        {
            // transmit the retrieved payload
            res = mDataCollectionSender->transmit( payload );
            if ( res != ConnectivityError::Success )
            {
                // Error occurred in the transmission
                mLogger.error( "IoTFleetWiseEngine::checkAndSendRetrievedData",
                               "Payload transmission failed, will be retried on the next bootup" );
                break;
            }
            else
            {
                mLogger.trace( "IoTFleetWiseEngine::checkAndSendRetrievedData",
                               "Payload has been successfully sent to the backend" );
            }
        }
        if ( res == ConnectivityError::Success )
        {
            // All the stored data has been transmitted, erase the file contents
            mPersistDecoderManifestCollectionSchemesAndData->erase( EDGE_TO_CLOUD_PAYLOAD );
            mLogger.info( "IoTFleetWiseEngine::checkAndSendRetrievedData",
                          "All Payloads successfully successfully sent to the backend" );
        }
    }
    else if ( status == EMPTY )
    {
        mLogger.info( "IoTFleetWiseEngine::checkAndSendRetrievedData", "No Payloads to Retrieve" );
    }
    else
    {
        mLogger.error( "IoTFleetWiseEngine::checkAndSendRetrievedData", "Payload Retrieval Failed" );
    }
}

} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
