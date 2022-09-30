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
#include "AwsBootstrap.h"
#include "CANDataConsumer.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionSchemeJSONParser.h"
#include "TraceModule.h"
#include "businterfaces/AbstractVehicleDataSource.h"
#include "businterfaces/CANDataSource.h"
#include <boost/lockfree/spsc_queue.hpp>

namespace Aws
{
namespace IoTFleetWise
{
namespace ExecutionManagement
{
using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::Platform::Linux::PersistencyManagement;
using Aws::IoTFleetWise::OffboardConnectivity::CollectionSchemeParams;
using Aws::IoTFleetWise::OffboardConnectivity::ConnectivityError;

const uint32_t IoTFleetWiseEngine::MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG = 6;
const uint64_t IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS = 1000;
const uint64_t IoTFleetWiseEngine::DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS = 10000;

static const std::string CAN_INTERFACE_TYPE = "canInterface";
static const std::string OBD_INTERFACE_TYPE = "obdInterface";

namespace
{

/**
 * @brief Get the File Contents including whitespace characters
 *
 * @param p The file path
 * @return std::string File contents
 */
std::string
getFileContents( const std::string &p )
{
    constexpr auto NUM_CHARS = 1;
    std::string ret;
    std::ifstream fs{ p };
    // False alarm: uninit_use_in_call: Using uninitialized value "fs._M_streambuf_state" when calling "good".
    // coverity[uninit_use_in_call : SUPPRESS]
    while ( fs.good() )
    {
        auto c = static_cast<char>( fs.get() );
        ret.append( NUM_CHARS, c );
    }

    return ret;
}

} // namespace

IoTFleetWiseEngine::IoTFleetWiseEngine()
{
    TraceModule::get().sectionBegin( TraceSection::FWE_STARTUP );
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

void
IoTFleetWiseEngine::attachVehicleDataSource( VehicleDataSourcePtr vehicleDataSource )
{
    mVehicleDataSource = std::move( vehicleDataSource );
}

void
IoTFleetWiseEngine::detachVehicleDataSource( VehicleDataSourcePtr vehicleDataSource )
{
    // TODO
    static_cast<void>( vehicleDataSource );
}

bool
IoTFleetWiseEngine::connect( const Json::Value &config )
{
    // Main bootstrap sequence.
    try
    {
        const auto persistencyPath = config["staticConfig"]["persistency"]["persistencyPath"].asString();
        /*************************Payload Manager and Persistency library bootstrap begin*********/

        // Create an object for Persistency
        mPersistDecoderManifestCollectionSchemesAndData = std::make_shared<CacheAndPersist>(
            persistencyPath, config["staticConfig"]["persistency"]["persistencyPartitionMaxSize"].asInt() );
        if ( !mPersistDecoderManifestCollectionSchemesAndData->init() )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to init persistency library " );
        }
        if ( config["staticConfig"]["persistency"].isMember( "PersistencyUploadRetryIntervalMs" ) )
        {
            mPersistencyUploadRetryIntervalMs =
                static_cast<uint64_t>( config["staticConfig"]["PersistencyUploadRetryIntervalMs"].asInt() );
        }
        else
        {
            mPersistencyUploadRetryIntervalMs = DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS;
        }
        // Payload Manager for offline data management
        mPayloadManager = std::make_shared<PayloadManager>( mPersistDecoderManifestCollectionSchemesAndData );

        /*************************Payload Manager and Persistency library bootstrap end************/

        /*************************CAN InterfaceID to InternalID Translator begin*********/
        CANInterfaceIDTranslator canIDTranslator;

        // Initialize
        for ( const auto &interfaceName : config["networkInterfaces"] )
        {
            if ( interfaceName["type"].asString() == CAN_INTERFACE_TYPE )
            {
                canIDTranslator.add( interfaceName["interfaceId"].asString() );
            }
        }
        /*************************CAN InterfaceID to InternalID Translator end*********/

        /**************************Connectivity bootstrap begin*******************************/

        mAwsIotModule = std::make_shared<AwsIotConnectivityModule>();

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
            config["staticConfig"]["internalParameters"]["useJsonBasedCollection"].asBool(),
            config["staticConfig"]["publishToCloudParameters"]["maxPublishMessageCount"].asUInt(),
            canIDTranslator,
            persistencyPath );

        // Pass on the AWS SDK Bootsrap handle to the IoTModule.
        auto bootstrapPtr = AwsBootstrap::getInstance().getClientBootStrap();

        const auto privateKey =
            getFileContents( config["staticConfig"]["mqttConnection"]["privateKeyFilename"].asString() );
        const auto certificate =
            getFileContents( config["staticConfig"]["mqttConnection"]["certificateFilename"].asString() );
        // For asynchronous connect the call needs to be done after all channels created and setTopic calls
        mAwsIotModule->connect( privateKey,
                                certificate,
                                config["staticConfig"]["mqttConnection"]["endpointUrl"].asString(),
                                config["staticConfig"]["mqttConnection"]["clientId"].asString(),
                                bootstrapPtr,
                                true );
        /*************************Connectivity `bootstrap end***************************************/

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
            mRemoteProfiler = std::make_unique<RemoteProfiler>(
                mAwsIotChannelMetricsUpload,
                mAwsIotChannelLogsUpload,
                config["staticConfig"]["remoteProfilerDefaultValues"]["metricsUploadIntervalMs"].asUInt(),
                config["staticConfig"]["remoteProfilerDefaultValues"]["loggingUploadMaxWaitBeforeUploadMs"].asUInt(),
                logThreshold,
                config["staticConfig"]["remoteProfilerDefaultValues"]["profilerPrefix"].asString() );
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

        // Below are three buffers to be shared between Vehicle Data Consumer and Collection Engine
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

        /*************************CollectionScheme Ingestion bootstrap begin*********************************/

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

        /********************************Vehicle Data Source Binder bootstrap start*******************************/

        auto obdOverCANModuleInit = false;
        // Start the vehicle data source binder
        mVehicleDataSourceBinder = std::make_unique<VehicleDataSourceBinder>();
        if ( mVehicleDataSourceBinder == nullptr || !mVehicleDataSourceBinder->connect() )
        {
            mLogger.error( "IoTFleetWiseEngine::connect", " Failed to initialize the Vehicle DataSource binder " );
            return false;
        }

        // Initialize
        for ( const auto &interfaceName : config["networkInterfaces"] )
        {
            const auto &interfaceType = interfaceName["type"].asString();

            if ( interfaceType == CAN_INTERFACE_TYPE )
            {
                std::vector<VehicleDataSourceConfig> canSourceConfigs( 1 );
                auto &canSourceConfig = canSourceConfigs.back();
                canSourceConfig.transportProperties.emplace(
                    "interfaceName", interfaceName[CAN_INTERFACE_TYPE]["interfaceName"].asString() );
                canSourceConfig.transportProperties.emplace(
                    "threadIdleTimeMs",
                    config["staticConfig"]["threadIdleTimes"]["socketCANThreadIdleTimeMs"].asString() );
                canSourceConfig.maxNumberOfVehicleDataMessages =
                    config["staticConfig"]["bufferSizes"]["socketCANBufferSize"].asUInt();
                CAN_TIMESTAMP_TYPE canTimestampType = CAN_TIMESTAMP_TYPE::KERNEL_SOFTWARE_TIMESTAMP; // default
                if ( interfaceName[CAN_INTERFACE_TYPE].isMember( "timestampType" ) )
                {
                    auto timestampTypeInput = interfaceName[CAN_INTERFACE_TYPE]["timestampType"].asString();
                    bool success = stringToCanTimestampType( timestampTypeInput, canTimestampType );
                    if ( !success )
                    {
                        mLogger.warn( "IoTFleetWiseEngine::connect",
                                      " Invalid can timestamp type provided: " + timestampTypeInput +
                                          " so default to Software" );
                    }
                }
                auto canSourcePtr = std::make_shared<CANDataSource>( canTimestampType );
                auto canConsumerPtr = std::make_shared<CANDataConsumer>();

                if ( canSourcePtr == nullptr || canConsumerPtr == nullptr )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to create consumer/producer " );
                    return false;
                }
                // Initialize the consumer/producers
                // Currently we limit 1 channel to a single consumer. We can always extend this
                // if we want to process the data coming from 1 channel to multiple consumers.
                if ( !canSourcePtr->init( canSourceConfigs ) ||
                     !canConsumerPtr->init(
                         static_cast<VehicleDataSourceID>(
                             canIDTranslator.getChannelNumericID( interfaceName["interfaceId"].asString() ) ),
                         signalBufferPtr,
                         config["staticConfig"]["threadIdleTimes"]["canDecoderThreadIdleTimeMs"].asUInt() ) )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to initialize the producers/consumers " );
                    return false;
                }
                else
                {
                    // CAN Consumers require a RAW Buffer after init
                    // TODO: This is temporary change . Will need to think how this can be abstracted for all data
                    // sources.
                    canConsumerPtr->setCANBufferPtr( canRawBufferPtr );
                }

                // Handshake the binder and the channel
                if ( !mVehicleDataSourceBinder->addVehicleDataSource( canSourcePtr ) )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to add a network channel " );
                    return false;
                }

                if ( !mVehicleDataSourceBinder->bindConsumerToVehicleDataSource(
                         canConsumerPtr, canSourcePtr->getVehicleDataSourceID() ) )
                {
                    mLogger.error( "IoTFleetWiseEngine::connect", " Failed to Bind Consumers to Producers " );
                    return false;
                }
            }
            else if ( interfaceType == OBD_INTERFACE_TYPE )
            {
                if ( !obdOverCANModuleInit )
                {
                    auto obdOverCANModule = std::make_shared<OBDOverCANModule>();
                    obdOverCANModuleInit = true;
                    // Init returns false if no collection is configured:
                    if ( obdOverCANModule->init(
                             signalBufferPtr,
                             activeDTCBufferPtr,
                             interfaceName[OBD_INTERFACE_TYPE]["interfaceName"].asString(),
                             interfaceName[OBD_INTERFACE_TYPE]["pidRequestIntervalSeconds"].asUInt(),
                             interfaceName[OBD_INTERFACE_TYPE]["dtcRequestIntervalSeconds"].asUInt(),
                             interfaceName[OBD_INTERFACE_TYPE]["useExtendedIds"].asBool(),
                             interfaceName[OBD_INTERFACE_TYPE]["hasTransmissionEcu"].asBool() ) )
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
        // Register Vehicle Data Source Binder as listener for CollectionScheme Manager
        if ( !mCollectionSchemeManagerPtr->subscribeListener( mVehicleDataSourceBinder.get() ) )
        {
            mLogger.error(
                "IoTFleetWiseEngine::connect",
                "Could not register the vehicle data source binder as a listener to the collection campaign manager" );
            return false;
        }

        /********************************Vehicle Data Source Binder bootstrap end*******************************/

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
            if ( mDataOverDDSModule == nullptr || !mDataOverDDSModule->init( ddsNodes ) )
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
    if ( mVehicleDataSourceBinder && !mVehicleDataSourceBinder->disconnect() )
    {
        mLogger.error( "IoTFleetWiseEngine::disconnect", "Could not disconnect the Binder" );
        return false;
    }
    mLogger.info( "IoTFleetWiseEngine::disconnect", "Engine Disconnected" );
    TraceModule::get().sectionEnd( TraceSection::FWE_SHUTDOWN );
    TraceModule::get().print();
    return true;
}

bool
IoTFleetWiseEngine::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
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
    TraceModule::get().sectionBegin( TraceSection::FWE_SHUTDOWN );
    std::lock_guard<std::mutex> lock( mThreadMutex );
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
    bool uploadedPersistedDataOnce = false;
    TraceModule::get().sectionEnd( TraceSection::FWE_STARTUP );

    engine->mRetrySendingPersistedDataTimer.reset();

    while ( !engine->shouldStop() )
    {
        // The interrupt arrives because of 2 reasons :
        // 1- An event trigger( either cyclic or interrupt ) has arrived, for this, all the buffers of the consumers
        // should be read and flushed.
        // 2- A consumer has been either reconnected or disconnected on runtime.
        // we should then either register or unregister for callbacks from this consumer.
        // 3- A new collectionScheme is available - First old data is sent out then the new collectionScheme is
        // applied to all consumers of the channel data

        engine->mTimer.reset();
        uint32_t elapsedTimeUs = 0;
        if ( !uploadedPersistedDataOnce )
        {
            // Minimum delay one tenth of FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS
            uint64_t timeToWaitMs =
                IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS -
                std::min( static_cast<uint64_t>( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ),
                          IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS );
            timeTrigger = static_cast<double>( std::max(
                              IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS / 10, timeToWaitMs ) ) /
                          1000.0;
        }
        else if ( engine->mPersistencyUploadRetryIntervalMs > 0 )
        {
            uint64_t timeToWaitMs =
                engine->mPersistencyUploadRetryIntervalMs -
                std::min( static_cast<uint64_t>( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ),
                          engine->mPersistencyUploadRetryIntervalMs );
            timeTrigger = static_cast<double>(
                              std::max( IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS, timeToWaitMs ) ) /
                          1000.0;
        }
        if ( timeTrigger > 0 )
        {
            engine->mLogger.trace(
                "IoTFleetWiseEngine::doWork",
                "Waiting for :" + std::to_string( timeTrigger ) + " seconds " +
                    std::to_string( engine->mPersistencyUploadRetryIntervalMs ) + " config" +
                    std::to_string( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) + " timer" );
            engine->mWait.wait( static_cast<uint32_t>( timeTrigger * 1000 ) );
        }
        else
        {
            engine->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
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
                std::string firstSignalTimestamp;
                for ( auto &s : triggeredCollectionSchemeDataPtr->signals )
                {
                    if ( firstSignalTimestamp.empty() )
                    {
                        firstSignalTimestamp = " first signal timestamp: " + std::to_string( s.receiveTime );
                    }
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
                    "FWE data ready to send with eventID " +
                        std::to_string( triggeredCollectionSchemeDataPtr->eventID ) + " from " +
                        triggeredCollectionSchemeDataPtr->metaData.collectionSchemeID +
                        " Signals:" + std::to_string( triggeredCollectionSchemeDataPtr->signals.size() ) + " " +
                        firstSignalValues + firstSignalTimestamp +
                        " raw CAN frames:" + std::to_string( triggeredCollectionSchemeDataPtr->canFrames.size() ) +
                        " DTCs:" + std::to_string( triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.size() ) +
                        " Geohash:" + triggeredCollectionSchemeDataPtr->mGeohashInfo.mGeohashString );
                engine->mDataCollectionSender->send( triggeredCollectionSchemeDataPtr );
            } );
        TraceModule::get().setVariable( TraceVariable::QUEUE_INSPECTION_TO_SENDER, consumedElements );

        if ( ( engine->mPersistencyUploadRetryIntervalMs > 0 &&
               ( static_cast<uint64_t>( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) >=
                 engine->mPersistencyUploadRetryIntervalMs ) ) ||
             ( !uploadedPersistedDataOnce &&
               ( static_cast<uint64_t>( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) >=
                 IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS ) ) )
        {
            engine->mRetrySendingPersistedDataTimer.reset();
            if ( engine->mAwsIotModule->isAlive() )
            {
                // Check if data was persisted, Retrieve all the data and send
                uploadedPersistedDataOnce |= engine->checkAndSendRetrievedData();
            }
        }
    }
}

void
IoTFleetWiseEngine::onDataReadyToPublish()
{
    mWait.notify();
}

bool
IoTFleetWiseEngine::checkAndSendRetrievedData()
{
    std::vector<std::string> payloads;

    // Retrieve the data from persistency library
    ErrorCode status = mPayloadManager->retrieveData( payloads );

    if ( status == ErrorCode::SUCCESS )
    {
        ConnectivityError res = ConnectivityError::Success;
        mLogger.trace( "IoTFleetWiseEngine::checkAndSendRetrievedData",
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
            mPersistDecoderManifestCollectionSchemesAndData->erase( DataType::EDGE_TO_CLOUD_PAYLOAD );
            mLogger.info( "IoTFleetWiseEngine::checkAndSendRetrievedData",
                          "All " + std::to_string( payloads.size() ) + " Payloads successfully sent to the backend" );
            return true;
        }
        return false;
    }
    else if ( status == ErrorCode::EMPTY )
    {
        mLogger.trace( "IoTFleetWiseEngine::checkAndSendRetrievedData", "No Payloads to Retrieve" );
        return true;
    }
    else
    {
        mLogger.error( "IoTFleetWiseEngine::checkAndSendRetrievedData", "Payload Retrieval Failed" );
        return false;
    }
}

} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
