// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "IoTFleetWiseEngine.h"
#include "AwsBootstrap.h"
#include "CollectionInspectionAPITypes.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <fstream>
#include <iomanip>
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
static const std::string EXTERNAL_CAN_INTERFACE_TYPE = "externalCanInterface";
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

#if defined( FWE_FEATURE_IWAVE_GPS ) || defined( FWE_FEATURE_EXTERNAL_GPS )
uint32_t
stringToU32( const std::string &value )
{
    try
    {
        return static_cast<uint32_t>( std::stoul( value ) );
    }
    catch ( const std::exception &e )
    {
        throw std::runtime_error( "Could not cast " + value +
                                  " to integer, invalid input: " + std::string( e.what() ) );
    }
}
#endif

} // namespace

IoTFleetWiseEngine::IoTFleetWiseEngine()
{
    TraceModule::get().sectionBegin( TraceSection::FWE_STARTUP );
}

IoTFleetWiseEngine::~IoTFleetWiseEngine()
{
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
        const auto persistencyPath = config["staticConfig"]["persistency"]["persistencyPath"].asString();
        /*************************Payload Manager and Persistency library bootstrap begin*********/

        // Create an object for Persistency
        mPersistDecoderManifestCollectionSchemesAndData = std::make_shared<CacheAndPersist>(
            persistencyPath, config["staticConfig"]["persistency"]["persistencyPartitionMaxSize"].asInt() );
        if ( !mPersistDecoderManifestCollectionSchemesAndData->init() )
        {
            FWE_LOG_ERROR( "Failed to init persistency library" );
        }
        if ( config["staticConfig"]["persistency"].isMember( "persistencyUploadRetryIntervalMs" ) )
        {
            mPersistencyUploadRetryIntervalMs =
                static_cast<uint64_t>( config["staticConfig"]["persistencyUploadRetryIntervalMs"].asInt() );
        }
        else
        {
            mPersistencyUploadRetryIntervalMs = DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS;
        }
        // Payload Manager for offline data management
        mPayloadManager = std::make_shared<PayloadManager>( mPersistDecoderManifestCollectionSchemesAndData );

        /*************************Payload Manager and Persistency library bootstrap end************/

        /*************************CAN InterfaceID to InternalID Translator begin*********/
        for ( const auto &interfaceName : config["networkInterfaces"] )
        {
            if ( ( interfaceName["type"].asString() == CAN_INTERFACE_TYPE ) ||
                 ( interfaceName["type"].asString() == EXTERNAL_CAN_INTERFACE_TYPE ) )
            {
                mCANIDTranslator.add( interfaceName["interfaceId"].asString() );
            }
        }
#ifdef FWE_FEATURE_IWAVE_GPS
        if ( config["staticConfig"].isMember( "iWaveGpsExample" ) )
        {
            mCANIDTranslator.add(
                config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::CAN_CHANNEL_NUMBER].asString() );
        }
        else
        {
            mCANIDTranslator.add( "IWAVE-GPS-CAN" );
        }
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
        if ( config["staticConfig"].isMember( "externalGpsExample" ) )
        {
            mCANIDTranslator.add(
                config["staticConfig"]["externalGpsExample"][ExternalGpsSource::CAN_CHANNEL_NUMBER].asString() );
        }
        else
        {
            mCANIDTranslator.add( "EXTERNAL-GPS-CAN" );
        }
#endif
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

        mDataCollectionSender = std::make_shared<DataCollectionSender>(
            mAwsIotChannelSendCanData,
            config["staticConfig"]["publishToCloudParameters"]["maxPublishMessageCount"].asUInt(),
            mCANIDTranslator );

        // Pass on the AWS SDK Bootstrap handle to the IoTModule.
        auto bootstrapPtr = AwsBootstrap::getInstance().getClientBootStrap();

        std::string privateKey;
        std::string certificate;
        std::string rootCA;
        if ( config["staticConfig"]["mqttConnection"].isMember( "privateKey" ) )
        {
            privateKey = config["staticConfig"]["mqttConnection"]["privateKey"].asString();
        }
        else if ( config["staticConfig"]["mqttConnection"].isMember( "privateKeyFilename" ) )
        {
            privateKey = getFileContents( config["staticConfig"]["mqttConnection"]["privateKeyFilename"].asString() );
        }
        if ( config["staticConfig"]["mqttConnection"].isMember( "certificate" ) )
        {
            certificate = config["staticConfig"]["mqttConnection"]["certificate"].asString();
        }
        else if ( config["staticConfig"]["mqttConnection"].isMember( "certificateFilename" ) )
        {
            certificate = getFileContents( config["staticConfig"]["mqttConnection"]["certificateFilename"].asString() );
        }
        if ( config["staticConfig"]["mqttConnection"].isMember( "rootCA" ) )
        {
            rootCA = config["staticConfig"]["mqttConnection"]["rootCA"].asString();
        }
        else if ( config["staticConfig"]["mqttConnection"].isMember( "rootCAFilename" ) )
        {
            rootCA = getFileContents( config["staticConfig"]["mqttConnection"]["rootCAFilename"].asString() );
        }
        // For asynchronous connect the call needs to be done after all channels created and setTopic calls
        mAwsIotModule->connect( privateKey,
                                certificate,
                                rootCA,
                                config["staticConfig"]["mqttConnection"]["endpointUrl"].asString(),
                                config["staticConfig"]["mqttConnection"]["clientId"].asString(),
                                bootstrapPtr,
                                true );
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
            mRemoteProfiler = std::make_unique<RemoteProfiler>(
                mAwsIotChannelMetricsUpload,
                mAwsIotChannelLogsUpload,
                config["staticConfig"]["remoteProfilerDefaultValues"]["metricsUploadIntervalMs"].asUInt(),
                config["staticConfig"]["remoteProfilerDefaultValues"]["loggingUploadMaxWaitBeforeUploadMs"].asUInt(),
                logThreshold,
                config["staticConfig"]["remoteProfilerDefaultValues"]["profilerPrefix"].asString() );
            if ( !mRemoteProfiler->start() )
            {
                FWE_LOG_WARN(

                    "Failed to start the Remote Profiler - No remote profiling available until FWE restart" );
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
        if ( ( !mCollectionInspectionWorkerThread->init(
                 signalBufferPtr,
                 canRawBufferPtr,
                 activeDTCBufferPtr,
                 mCollectedDataReadyToPublish,
                 config["staticConfig"]["threadIdleTimes"]["inspectionThreadIdleTimeMs"].asUInt(),
                 config["staticConfig"]["internalParameters"]["dataReductionProbabilityDisabled"].asBool() ) ) ||
             ( !mCollectionInspectionWorkerThread->start() ) )
        {
            FWE_LOG_ERROR( "Failed to init and start the Inspection Engine" );
            return false;
        }
        // Make sure the Inspection Engine can notify the Bootstrap thread about ready to be
        // published data.
        if ( !mCollectionInspectionWorkerThread->subscribeListener( this ) )
        {
            FWE_LOG_ERROR( "Failed register the Engine Thread to the Inspection Module" );
            return false;
        }

        /*************************Inspection Engine bootstrap end***********************************/

        /*************************CollectionScheme Ingestion bootstrap begin*********************************/

        // CollectionScheme Ingestion module executes in the context for the offboardconnectivity thread. Upcoming
        // messages are expected to come either on the decoder manifest topic or the collectionScheme topic or both
        // ( eventually ).
        mSchemaPtr = std::make_shared<Schema>( mAwsIotChannelReceiveDecoderManifest,
                                               mAwsIotChannelReceiveCollectionSchemeList,
                                               mAwsIotChannelSendCheckin );

        /*****************************CollectionScheme Management bootstrap begin*****************************/

        // Create and connect the CollectionScheme Manager
        mCollectionSchemeManagerPtr = std::make_shared<CollectionSchemeManager>();

        if ( !mCollectionSchemeManagerPtr->init(
                 config["staticConfig"]["publishToCloudParameters"]["collectionSchemeManagementCheckinIntervalMs"]
                     .asUInt(),
                 mPersistDecoderManifestCollectionSchemesAndData,
                 mCANIDTranslator ) )
        {
            FWE_LOG_ERROR( "Failed to init the CollectionScheme Manager" );
            return false;
        }

        // Make sure the CollectionScheme Ingestion can notify the CollectionScheme Manager about the arrival
        // of new artifacts over the offboardconnectivity channel.
        if ( !mSchemaPtr->subscribeListener(
                 static_cast<CollectionSchemeManagementListener *>( mCollectionSchemeManagerPtr.get() ) ) )
        {
            FWE_LOG_ERROR( "Failed register the CollectionScheme Manager to the CollectionScheme Ingestion Module" );
            return false;
        }

        // Make sure the CollectionScheme Manager can notify the Inspection Engine about the availability of
        // a new set of collection CollectionSchemes.
        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                 static_cast<IActiveConditionProcessor *>( mCollectionInspectionWorkerThread.get() ) ) )
        {
            FWE_LOG_ERROR( "Failed register the Inspection Engine to the CollectionScheme Manager Module" );
            return false;
        }

        // Allow CollectionSchemeManagement to send checkins through the Schema Object Callback
        mCollectionSchemeManagerPtr->setSchemaListenerPtr( mSchemaPtr );

        /********************************Data source bootstrap start*******************************/

        auto obdOverCANModuleInit = false;
        mCANDataConsumer = std::make_unique<CANDataConsumer>( signalBufferPtr, canRawBufferPtr );
        for ( const auto &interfaceName : config["networkInterfaces"] )
        {
            const auto &interfaceType = interfaceName["type"].asString();

            if ( interfaceType == CAN_INTERFACE_TYPE )
            {
                CanTimestampType canTimestampType = CanTimestampType::KERNEL_SOFTWARE_TIMESTAMP; // default
                if ( interfaceName[CAN_INTERFACE_TYPE].isMember( "timestampType" ) )
                {
                    auto timestampTypeInput = interfaceName[CAN_INTERFACE_TYPE]["timestampType"].asString();
                    bool success = stringToCanTimestampType( timestampTypeInput, canTimestampType );
                    if ( !success )
                    {
                        FWE_LOG_WARN( "Invalid can timestamp type provided: " + timestampTypeInput +
                                      " so default to Software" );
                    }
                }
                auto canChannelId = mCANIDTranslator.getChannelNumericID( interfaceName["interfaceId"].asString() );
                auto canSourcePtr = std::make_unique<CANDataSource>(
                    canChannelId,
                    canTimestampType,
                    interfaceName[CAN_INTERFACE_TYPE]["interfaceName"].asString(),
                    interfaceName[CAN_INTERFACE_TYPE]["protocolName"].asString() == "CAN-FD",
                    config["staticConfig"]["threadIdleTimes"]["socketCANThreadIdleTimeMs"].asUInt(),
                    *mCANDataConsumer );
                if ( !canSourcePtr->init() )
                {
                    FWE_LOG_ERROR( "Failed to initialize CANDataSource" );
                    return false;
                }
                if ( !mCollectionSchemeManagerPtr->subscribeListener(
                         static_cast<IActiveDecoderDictionaryListener *>( canSourcePtr.get() ) ) )
                {
                    FWE_LOG_ERROR( "Failed to register the CANDataSource to the CollectionScheme Manager" );
                    return false;
                }
                mCANDataSources.push_back( std::move( canSourcePtr ) );
            }
            else if ( interfaceType == OBD_INTERFACE_TYPE )
            {
                if ( !obdOverCANModuleInit )
                {
                    auto obdOverCANModule = std::make_shared<OBDOverCANModule>();
                    obdOverCANModuleInit = true;
                    const auto &broadcastRequests = interfaceName[OBD_INTERFACE_TYPE]["broadcastRequests"];
                    if ( obdOverCANModule->init(
                             signalBufferPtr,
                             activeDTCBufferPtr,
                             interfaceName[OBD_INTERFACE_TYPE]["interfaceName"].asString(),
                             interfaceName[OBD_INTERFACE_TYPE]["pidRequestIntervalSeconds"].asUInt(),
                             interfaceName[OBD_INTERFACE_TYPE]["dtcRequestIntervalSeconds"].asUInt(),
                             // Broadcast mode is enabled by default if not defined in config:
                             broadcastRequests.isNull() || broadcastRequests.asBool() ) )
                    {
                        // Connect the OBD Module
                        mOBDOverCANModule = obdOverCANModule;
                        if ( !mOBDOverCANModule->connect() )
                        {
                            FWE_LOG_ERROR( "Failed to connect OBD over CAN module" );
                            return false;
                        }

                        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                                 static_cast<IActiveDecoderDictionaryListener *>( mOBDOverCANModule.get() ) ) )
                        {
                            FWE_LOG_ERROR( "Failed to register the OBD Module to the CollectionScheme Manager" );
                            return false;
                        }
                        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                                 static_cast<IActiveConditionProcessor *>( mOBDOverCANModule.get() ) ) )
                        {
                            FWE_LOG_ERROR( "Failed to register the OBD Module to the CollectionScheme Manager" );
                            return false;
                        }
                    }
                }
                else
                {
                    FWE_LOG_ERROR( "obdOverCANModule already initialised" );
                }
            }
            else if ( interfaceType == EXTERNAL_CAN_INTERFACE_TYPE )
            {
                if ( mExternalCANDataSource == nullptr )
                {
                    mExternalCANDataSource = std::make_unique<ExternalCANDataSource>( *mCANDataConsumer );
                    if ( !mCollectionSchemeManagerPtr->subscribeListener(
                             static_cast<IActiveDecoderDictionaryListener *>( mExternalCANDataSource.get() ) ) )
                    {
                        FWE_LOG_ERROR( "Failed to register the ExternalCANDataSource to the CollectionScheme Manager" );
                        return false;
                    }
                }
            }
            else
            {
                FWE_LOG_ERROR( interfaceName["type"].asString() + " is not supported" );
            }
        }

        /********************************Data source bootstrap end*******************************/

        // Only start the CollectionSchemeManager after all listeners have subscribed, otherwise
        // they will not be notified of the initial decoder manifest and collection schemes that are
        // read from persistent memory:
        if ( !mCollectionSchemeManagerPtr->connect() )
        {
            FWE_LOG_ERROR( "Failed to start the CollectionScheme Manager" );
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
                FWE_LOG_WARN( "Unsupported Transport config provided for a DDS Node, skipping it" );
                continue;
            }

            // Device Type, currently only CAMERA is supported
            if ( ddsNode["dds-device-type"].asString() == "CAMERA" )
            {
                nodeConfig.sourceType = SensorSourceType::CAMERA;
            }
            else
            {
                FWE_LOG_WARN( "Unsupported Device type provided for a DDS Node, skipping it" );
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
            mDataOverDDSModule = std::make_shared<DataOverDDSModule>();
            // Init the Module
            if ( !mDataOverDDSModule->init( ddsNodes ) )
            {
                FWE_LOG_ERROR( "Failed to initialize the DDS Module" );
                return false;
            }
            // Register the DDS Module as a listener to the Inspection Engine and connect it.
            if ( ( !mCollectionInspectionWorkerThread->subscribeToEvents(
                     static_cast<InspectionEventListener *>( mDataOverDDSModule.get() ) ) ) ||
                 ( !mDataOverDDSModule->connect() ) )
            {
                FWE_LOG_ERROR( "Failed to connect the DDS Module" );
                return false;
            }
            FWE_LOG_INFO( "DDS Module connected" );
        }
        else
        {
            FWE_LOG_INFO( "DDS Module disabled" );
        }

        /********************************DDS Module bootstrap end*********************************/
#endif // FWE_FEATURE_CAMERA

#ifdef FWE_FEATURE_IWAVE_GPS
        /********************************IWave GPS Example NMEA reader *********************************/
        mIWaveGpsSource = std::make_shared<IWaveGpsSource>( signalBufferPtr );
        bool iWaveInitSuccessful = false;
        if ( config["staticConfig"].isMember( "iWaveGpsExample" ) )
        {
            FWE_LOG_TRACE( "Found 'iWaveGpsExample' section in config file" );
            iWaveInitSuccessful = mIWaveGpsSource->init(
                config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::PATH_TO_NMEA].asString(),
                mCANIDTranslator.getChannelNumericID(
                    config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::CAN_CHANNEL_NUMBER].asString() ),
                stringToU32( config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::CAN_RAW_FRAME_ID].asString() ),
                static_cast<uint16_t>( stringToU32(
                    config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::LATITUDE_START_BIT].asString() ) ),
                static_cast<uint16_t>( stringToU32(
                    config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::LONGITUDE_START_BIT].asString() ) ) );
        }
        else
        {
            // If not config available default to this values
            iWaveInitSuccessful = mIWaveGpsSource->init(
                "/dev/ttyUSB1", mCANIDTranslator.getChannelNumericID( "IWAVE-GPS-CAN" ), 1, 32, 0 );
        }
        if ( iWaveInitSuccessful && mIWaveGpsSource->connect() )
        {
            if ( !mCollectionSchemeManagerPtr->subscribeListener(
                     static_cast<IActiveDecoderDictionaryListener *>( mIWaveGpsSource.get() ) ) )
            {
                FWE_LOG_ERROR( "Failed to register the IWaveGps to the CollectionScheme Manager" );
                return false;
            }
            mIWaveGpsSource->start();
        }
        else
        {
            FWE_LOG_ERROR( "IWaveGps initialization failed" );
            return false;
        }
        /********************************IWave GPS Example NMEA reader end******************************/
#endif

#ifdef FWE_FEATURE_EXTERNAL_GPS
        /********************************External GPS Example NMEA reader *********************************/
        mExternalGpsSource = std::make_shared<ExternalGpsSource>( signalBufferPtr );
        bool externalGpsInitSuccessful = false;
        if ( config["staticConfig"].isMember( "externalGpsExample" ) )
        {
            FWE_LOG_TRACE( "Found 'externalGpsExample' section in config file" );
            externalGpsInitSuccessful = mExternalGpsSource->init(
                mCANIDTranslator.getChannelNumericID(
                    config["staticConfig"]["externalGpsExample"][ExternalGpsSource::CAN_CHANNEL_NUMBER].asString() ),
                stringToU32(
                    config["staticConfig"]["externalGpsExample"][ExternalGpsSource::CAN_RAW_FRAME_ID].asString() ),
                static_cast<uint16_t>( stringToU32(
                    config["staticConfig"]["externalGpsExample"][ExternalGpsSource::LATITUDE_START_BIT].asString() ) ),
                static_cast<uint16_t>(
                    stringToU32( config["staticConfig"]["externalGpsExample"][ExternalGpsSource::LONGITUDE_START_BIT]
                                     .asString() ) ) );
        }
        else
        {
            // If not config available default to this values
            externalGpsInitSuccessful =
                mExternalGpsSource->init( mCANIDTranslator.getChannelNumericID( "EXTERNAL-GPS-CAN" ), 1, 32, 0 );
        }
        if ( externalGpsInitSuccessful )
        {
            if ( !mCollectionSchemeManagerPtr->subscribeListener(
                     static_cast<IActiveDecoderDictionaryListener *>( mExternalGpsSource.get() ) ) )
            {
                FWE_LOG_ERROR( "Failed to register the ExternalGpsSource to the CollectionScheme Manager" );
                return false;
            }
            mExternalGpsSource->start();
        }
        else
        {
            FWE_LOG_ERROR( "ExternalGpsSource initialization failed" );
            return false;
        }
        /********************************External GPS Example NMEA reader end******************************/
#endif

        mPrintMetricsCyclicPeriodMs =
            config["staticConfig"]["internalParameters"]["metricsCyclicPrintIntervalMs"].asUInt(); // default to 0
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Fatal Error during AWS IoT FleetWise Bootstrap: " + std::string( e.what() ) );
        return false;
    }

    FWE_LOG_INFO( "Engine Connected" );

    return true;
}

bool
IoTFleetWiseEngine::disconnect()
{
#ifdef FWE_FEATURE_EXTERNAL_GPS
    if ( mExternalGpsSource )
    {
        mExternalGpsSource->stop();
    }
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
    if ( mIWaveGpsSource )
    {
        mIWaveGpsSource->stop();
    }
#endif
#ifdef FWE_FEATURE_CAMERA
    if ( mDataOverDDSModule )
    {
        if ( ( !mCollectionInspectionWorkerThread->unSubscribeFromEvents(
                 static_cast<InspectionEventListener *>( mDataOverDDSModule.get() ) ) ) ||
             ( !mDataOverDDSModule->disconnect() ) )
        {

            FWE_LOG_ERROR( "Could not disconnect DDS Module" );
            return false;
        }
    }
#endif // FWE_FEATURE_CAMERA

    if ( mOBDOverCANModule )
    {
        if ( !mOBDOverCANModule->disconnect() )
        {
            FWE_LOG_ERROR( "Could not disconnect OBD over CAN module" );
            return false;
        }
    }

    if ( !mCollectionInspectionWorkerThread->stop() )
    {
        FWE_LOG_ERROR( "Could not stop the Inspection Engine" );
        return false;
    }

    setLogForwarding( nullptr );
    if ( ( mRemoteProfiler != nullptr ) && ( !mRemoteProfiler->stop() ) )
    {
        FWE_LOG_ERROR( "Could not stop the Remote Profiler" );
        return false;
    }

    if ( !mCollectionSchemeManagerPtr->disconnect() )
    {
        FWE_LOG_ERROR( "Could not stop the CollectionScheme Manager" );
        return false;
    }

    for ( auto &source : mCANDataSources )
    {
        if ( !source->disconnect() )
        {
            FWE_LOG_ERROR( "Could not disconnect CAN data source" );
            return false;
        }
    }

    if ( mAwsIotModule->isAlive() && ( !mAwsIotModule->disconnect() ) )
    {
        FWE_LOG_ERROR( "Could not disconnect the off-board connectivity" );
        return false;
    }
    FWE_LOG_INFO( "Engine Disconnected" );
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
        FWE_LOG_TRACE( "Engine Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Engine Thread started" );
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

        if ( engine->mPrintMetricsCyclicPeriodMs != 0 )
        {
            uint64_t timeToWaitMs =
                engine->mPrintMetricsCyclicPeriodMs -
                std::min( static_cast<uint64_t>( engine->mPrintMetricsCyclicTimer.getElapsedMs().count() ),
                          engine->mPrintMetricsCyclicPeriodMs );
            timeTrigger = std::min( timeTrigger, static_cast<double>( timeToWaitMs ) / 1000.0 );
        }
        if ( timeTrigger > 0 )
        {
            FWE_LOG_TRACE( "Waiting for :" + std::to_string( timeTrigger ) + " seconds. Persistency " +
                           std::to_string( engine->mPersistencyUploadRetryIntervalMs ) + " configured, " +
                           std::to_string( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) +
                           " timer. Cyclic Metrics Print:" + std::to_string( engine->mPrintMetricsCyclicPeriodMs ) +
                           " configured,  " +
                           std::to_string( engine->mPrintMetricsCyclicTimer.getElapsedMs().count() ) + " timer." );
            engine->mWait.wait( static_cast<uint32_t>( timeTrigger * 1000 ) );
        }
        else
        {
            engine->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
            elapsedTimeUs += static_cast<uint32_t>( engine->mTimer.getElapsedMs().count() );
            FWE_LOG_TRACE( "Event Arrived" );
            FWE_LOG_TRACE( "Time Elapsed waiting for the Event : " + std::to_string( elapsedTimeUs ) );
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
                    auto signalValue = s.getValue();
                    firstSignalValues += std::to_string( s.signalID ) + ":";
                    switch ( signalValue.getType() )
                    {
                    case SignalType::UINT8:
                        firstSignalValues += std::to_string( signalValue.value.uint8Val ) + ",";
                        break;
                    case SignalType::INT8:
                        firstSignalValues += std::to_string( signalValue.value.int8Val ) + ",";
                        break;
                    case SignalType::UINT16:
                        firstSignalValues += std::to_string( signalValue.value.uint16Val ) + ",";
                        break;
                    case SignalType::INT16:
                        firstSignalValues += std::to_string( signalValue.value.int16Val ) + ",";
                        break;
                    case SignalType::UINT32:
                        firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
                        break;
                    case SignalType::INT32:
                        firstSignalValues += std::to_string( signalValue.value.int32Val ) + ",";
                        break;
                    case SignalType::UINT64:
                        firstSignalValues += std::to_string( signalValue.value.uint64Val ) + ",";
                        break;
                    case SignalType::INT64:
                        firstSignalValues += std::to_string( signalValue.value.int64Val ) + ",";
                        break;
                    case SignalType::FLOAT:
                        firstSignalValues += std::to_string( signalValue.value.floatVal ) + ",";
                        break;
                    case SignalType::DOUBLE:
                        firstSignalValues += std::to_string( signalValue.value.doubleVal ) + ",";
                        break;
                    case SignalType::BOOLEAN:
                        firstSignalValues += std::to_string( static_cast<int>( signalValue.value.boolVal ) ) + ",";
                        break;
                    default:
                        firstSignalValues += std::to_string( signalValue.value.doubleVal ) + ",";
                        break;
                    }
                }
                firstSignalValues += "]";
                // Avoid invoking Data Collection Sender if there is nothing to send.
                if ( triggeredCollectionSchemeDataPtr->signals.empty() &&
                     triggeredCollectionSchemeDataPtr->canFrames.empty() &&
                     triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.empty() &&
                     ( !triggeredCollectionSchemeDataPtr->mGeohashInfo.hasItems() ) )
                {
                    FWE_LOG_INFO(
                        "The trigger for Campaign:  " + triggeredCollectionSchemeDataPtr->metaData.collectionSchemeID +
                        " activated eventID: " + std::to_string( triggeredCollectionSchemeDataPtr->eventID ) +
                        " but no data is available to ingest" );
                }
                else
                {
                    FWE_LOG_INFO( "FWE data ready to send with eventID " +
                                  std::to_string( triggeredCollectionSchemeDataPtr->eventID ) + " from " +
                                  triggeredCollectionSchemeDataPtr->metaData.collectionSchemeID +
                                  " Signals:" + std::to_string( triggeredCollectionSchemeDataPtr->signals.size() ) +
                                  " " + firstSignalValues + firstSignalTimestamp + " raw CAN frames:" +
                                  std::to_string( triggeredCollectionSchemeDataPtr->canFrames.size() ) + " DTCs:" +
                                  std::to_string( triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes.size() ) +
                                  " Geohash:" + triggeredCollectionSchemeDataPtr->mGeohashInfo.mGeohashString );
                    engine->mDataCollectionSender->send( triggeredCollectionSchemeDataPtr );
                }
            } );
        TraceModule::get().setVariable( TraceVariable::QUEUE_INSPECTION_TO_SENDER, consumedElements );

        if ( ( ( engine->mPersistencyUploadRetryIntervalMs > 0 ) &&
               ( static_cast<uint64_t>( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) >=
                 engine->mPersistencyUploadRetryIntervalMs ) ) ||
             ( ( !uploadedPersistedDataOnce ) &&
               ( static_cast<uint64_t>( engine->mRetrySendingPersistedDataTimer.getElapsedMs().count() ) >=
                 IoTFleetWiseEngine::FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS ) ) )
        {
            engine->mRetrySendingPersistedDataTimer.reset();
            if ( engine->mAwsIotModule->isAlive() && engine->checkAndSendRetrievedData() )
            {
                // Check if data was persisted, Retrieve all the data and send
                uploadedPersistedDataOnce = true;
            }
        }
        if ( ( engine->mPrintMetricsCyclicPeriodMs > 0 ) &&
             ( static_cast<uint64_t>( engine->mPrintMetricsCyclicTimer.getElapsedMs().count() ) >=
               engine->mPrintMetricsCyclicPeriodMs ) )
        {
            engine->mPrintMetricsCyclicTimer.reset();
            TraceModule::get().print();
            TraceModule::get().startNewObservationWindow(
                static_cast<uint32_t>( engine->mPrintMetricsCyclicPeriodMs ) );
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
        FWE_LOG_TRACE( "Number of Payloads to transmit : " + std::to_string( payloads.size() ) );

        for ( const auto &payload : payloads )
        {
            // transmit the retrieved payload
            res = mDataCollectionSender->transmit( payload );
            if ( res != ConnectivityError::Success )
            {
                // Error occurred in the transmission
                FWE_LOG_ERROR( "Payload transmission failed, will be retried on the next bootup" );
                break;
            }
            else
            {
                FWE_LOG_TRACE( "Payload has been successfully sent to the backend" );
            }
        }
        if ( res == ConnectivityError::Success )
        {
            // All the stored data has been transmitted, erase the file contents
            mPersistDecoderManifestCollectionSchemesAndData->erase( DataType::EDGE_TO_CLOUD_PAYLOAD );
            FWE_LOG_INFO( "All " + std::to_string( payloads.size() ) + " Payloads successfully sent to the backend" );
            return true;
        }
        return false;
    }
    else if ( status == ErrorCode::EMPTY )
    {
        FWE_LOG_TRACE( "No Payloads to Retrieve" );
        return true;
    }
    else
    {
        FWE_LOG_ERROR( "Payload Retrieval Failed" );
        return false;
    }
}

std::vector<uint8_t>
IoTFleetWiseEngine::getExternalOBDPIDsToRequest()
{
    std::vector<uint8_t> pids;
    if ( mOBDOverCANModule != nullptr )
    {
        pids = mOBDOverCANModule->getExternalPIDsToRequest();
    }
    return pids;
}

void
IoTFleetWiseEngine::setExternalOBDPIDResponse( PID pid, const std::vector<uint8_t> &response )
{
    if ( mOBDOverCANModule == nullptr )
    {
        return;
    }
    mOBDOverCANModule->setExternalPIDResponse( pid, response );
}

void
IoTFleetWiseEngine::ingestExternalCANMessage( const std::string &interfaceId,
                                              Timestamp timestamp,
                                              uint32_t messageId,
                                              const std::vector<uint8_t> &data )
{
    auto canChannelId = mCANIDTranslator.getChannelNumericID( interfaceId );
    if ( canChannelId == INVALID_CAN_SOURCE_NUMERIC_ID )
    {
        FWE_LOG_ERROR( "Unknown interface ID: " + interfaceId );
        return;
    }
    if ( mExternalCANDataSource == nullptr )
    {
        FWE_LOG_ERROR( "No external CAN interface present" );
        return;
    }
    mExternalCANDataSource->ingestMessage( canChannelId, timestamp, messageId, data );
}

#ifdef FWE_FEATURE_EXTERNAL_GPS
void
IoTFleetWiseEngine::setExternalGpsLocation( double latitude, double longitude )
{
    if ( mExternalGpsSource == nullptr )
    {
        return;
    }
    mExternalGpsSource->setLocation( latitude, longitude );
}
#endif

std::string
IoTFleetWiseEngine::getStatusSummary()
{
    if ( mAwsIotModule == nullptr || mCollectionSchemeManagerPtr == nullptr || mAwsIotChannelSendCanData == nullptr ||
         mOBDOverCANModule == nullptr
#ifdef FWE_FEATURE_EXTERNAL_GPS
         || mExternalGpsSource == nullptr
#endif
    )
    {
        return "";
    }
    std::string status;
    status +=
        std::string( "MQTT connection: " ) + ( mAwsIotModule->isAlive() ? "CONNECTED" : "NOT CONNECTED" ) + "\n\n";

    status += "Campaign ARNs:\n";
    auto collectionSchemeArns = mCollectionSchemeManagerPtr->getCollectionSchemeArns();
    if ( collectionSchemeArns.empty() )
    {
        status += "NONE\n";
    }
    else
    {
        for ( auto &collectionSchemeArn : collectionSchemeArns )
        {
            status += collectionSchemeArn + "\n";
        }
    }
    status += "\n";

    status += "Payloads sent: " + std::to_string( mAwsIotChannelSendCanData->getPayloadCountSent() ) + "\n\n";
    return status;
}

} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
