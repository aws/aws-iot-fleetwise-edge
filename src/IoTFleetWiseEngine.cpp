// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IoTFleetWiseEngine.h"
#include "AwsBootstrap.h"
#include "AwsIotConnectivityModule.h"
#include "AwsSDKMemoryManager.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionSchemeManagementListener.h"
#include "DataSenderManager.h"
#include "IActiveConditionProcessor.h"
#include "IActiveDecoderDictionaryListener.h"
#include "ILogger.h"
#include "LogLevel.h"
#include "LoggingModule.h"
#include "MqttClientWrapper.h"
#include "SignalTypes.h"
#include "TraceModule.h"
#include <algorithm>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/iot/Mqtt5Client.h>
#include <cstddef>
#include <exception>
#include <fstream>
#include <utility>

#if defined( FWE_FEATURE_IWAVE_GPS ) || defined( FWE_FEATURE_EXTERNAL_GPS )
#include <stdexcept>
#endif
#ifdef FWE_FEATURE_GREENGRASSV2
#include "AwsGGConnectivityModule.h"
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#endif
#endif
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "Credentials.h"
#include "DataSenderIonWriter.h"
#include "IActiveCollectionSchemesListener.h"
#include "RawDataManager.h"
#include "TransferManagerWrapper.h"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/S3ServiceClientModel.h>
#include <aws/transfer/TransferManager.h>
#include <boost/move/utility_core.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#endif

namespace Aws
{
namespace IoTFleetWise
{

static const std::string CAN_INTERFACE_TYPE = "canInterface";
static const std::string EXTERNAL_CAN_INTERFACE_TYPE = "externalCanInterface";
static const std::string OBD_INTERFACE_TYPE = "obdInterface";
#ifdef FWE_FEATURE_ROS2
static const std::string ROS2_INTERFACE_TYPE = "ros2Interface";
#endif

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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
size_t
getJsonValueAsSize( const Json::Value &jsonValue )
{
    return sizeof( size_t ) >= sizeof( uint64_t ) ? jsonValue.asUInt64() : jsonValue.asUInt();
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
        uint64_t persistencyUploadRetryIntervalMs = DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS;
        if ( config["staticConfig"]["persistency"].isMember( "persistencyUploadRetryIntervalMs" ) )
        {
            persistencyUploadRetryIntervalMs = static_cast<uint64_t>(
                config["staticConfig"]["persistency"]["persistencyUploadRetryIntervalMs"].asInt() );
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
#ifdef FWE_FEATURE_AAOS_VHAL
        if ( config["staticConfig"].isMember( "aaosVhalExample" ) )
        {
            mCANIDTranslator.add(
                config["staticConfig"]["aaosVhalExample"][AaosVhalSource::CAN_CHANNEL_NUMBER].asString() );
        }
        else
        {
            mCANIDTranslator.add( "AAOS-VHAL-CAN" );
        }
#endif
        /*************************CAN InterfaceID to InternalID Translator end*********/

        /**************************Connectivity bootstrap begin*******************************/
        // Pass on the AWS SDK Bootstrap handle to the IoTModule.
        auto bootstrapPtr = AwsBootstrap::getInstance().getClientBootStrap();
        std::size_t maxAwsSdkHeapMemoryBytes = 0U;
        if ( config["staticConfig"]["internalParameters"].isMember( "maximumAwsSdkHeapMemoryBytes" ) )
        {
            maxAwsSdkHeapMemoryBytes =
                config["staticConfig"]["internalParameters"]["maximumAwsSdkHeapMemoryBytes"].asUInt();
            if ( ( maxAwsSdkHeapMemoryBytes != 0U ) &&
                 AwsSDKMemoryManager::getInstance().setLimit( maxAwsSdkHeapMemoryBytes ) )
            {
                FWE_LOG_INFO( "Maximum AWS SDK Heap Memory Bytes has been configured:" +
                              std::to_string( maxAwsSdkHeapMemoryBytes ) );
            }
            else
            {
                FWE_LOG_TRACE( "Maximum AWS SDK Heap Memory Bytes will use default value" );
            }
        }
        else
        {
            FWE_LOG_TRACE( "Maximum AWS SDK Heap Memory Bytes will use default value" );
        }

#ifdef FWE_FEATURE_GREENGRASSV2
        if ( config["staticConfig"]["mqttConnection"]["connectionType"].asString() == "iotGreengrassV2" )
        {
            FWE_LOG_INFO( "ConnectionType is iotGreengrassV2" )
            mConnectivityModule = std::make_shared<AwsGGConnectivityModule>( bootstrapPtr );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            mAwsCredentialsProvider = std::make_shared<Aws::Auth::DefaultAWSCredentialsProviderChain>();
#endif
        }
        else
#endif
        {
            std::string privateKey;
            std::string certificate;
            std::string rootCA;
            FWE_LOG_INFO( "ConnectionType is iotCore " +
                          config["staticConfig"]["mqttConnection"]["connectionType"].asString() )
            // fetch connection parameters from config
            if ( config["staticConfig"]["mqttConnection"].isMember( "privateKey" ) )
            {
                privateKey = config["staticConfig"]["mqttConnection"]["privateKey"].asString();
            }
            else if ( config["staticConfig"]["mqttConnection"].isMember( "privateKeyFilename" ) )
            {
                privateKey =
                    getFileContents( config["staticConfig"]["mqttConnection"]["privateKeyFilename"].asString() );
            }
            if ( config["staticConfig"]["mqttConnection"].isMember( "certificate" ) )
            {
                certificate = config["staticConfig"]["mqttConnection"]["certificate"].asString();
            }
            else if ( config["staticConfig"]["mqttConnection"].isMember( "certificateFilename" ) )
            {
                certificate =
                    getFileContents( config["staticConfig"]["mqttConnection"]["certificateFilename"].asString() );
            }
            if ( config["staticConfig"]["mqttConnection"].isMember( "rootCA" ) )
            {
                rootCA = config["staticConfig"]["mqttConnection"]["rootCA"].asString();
            }
            else if ( config["staticConfig"]["mqttConnection"].isMember( "rootCAFilename" ) )
            {
                rootCA = getFileContents( config["staticConfig"]["mqttConnection"]["rootCAFilename"].asString() );
            }
            // coverity[autosar_cpp14_a20_8_5_violation] - can't use make_unique as the constructor is private
            auto builder = std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder>(
                Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromMemory(
                    config["staticConfig"]["mqttConnection"]["endpointUrl"].asString().c_str(),
                    Crt::ByteCursorFromCString( certificate.c_str() ),
                    Crt::ByteCursorFromCString( privateKey.c_str() ) ) );

            std::unique_ptr<MqttClientBuilderWrapper> builderWrapper;
            if ( builder == nullptr )
            {
                FWE_LOG_ERROR( "Failed to setup mqtt5 client builder with error code " +
                               std::to_string( Aws::Crt::LastError() ) + ": " +
                               Aws::Crt::ErrorDebugString( Aws::Crt::LastError() ) );
            }
            else
            {
                builder->WithBootstrap( bootstrapPtr );
                builderWrapper = std::make_unique<MqttClientBuilderWrapper>( std::move( builder ) );
            }

            mConnectivityModule = std::make_shared<AwsIotConnectivityModule>(
                rootCA, config["staticConfig"]["mqttConnection"]["clientId"].asString(), std::move( builderWrapper ) );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            if ( config["staticConfig"].isMember( "credentialsProvider" ) )
            {
                auto crtCredentialsProvider = createX509CredentialsProvider(
                    bootstrapPtr,
                    config["staticConfig"]["mqttConnection"]["clientId"].asString(),
                    privateKey,
                    certificate,
                    config["staticConfig"]["credentialsProvider"]["endpointUrl"].asString(),
                    config["staticConfig"]["credentialsProvider"]["roleAlias"].asString() );
                mAwsCredentialsProvider = std::make_shared<CrtCredentialsProviderAdapter>( crtCredentialsProvider );
            }
#endif
        }

        // Only CAN data channel needs a payloadManager object for persistency and compression support,
        // for other components this will be nullptr
        mConnectivityChannelSendVehicleData = mConnectivityModule->createNewChannel(
            mPayloadManager, config["staticConfig"]["mqttConnection"]["canDataTopic"].asString() );

        mConnectivityChannelReceiveCollectionSchemeList = mConnectivityModule->createNewChannel(
            nullptr, config["staticConfig"]["mqttConnection"]["collectionSchemeListTopic"].asString(), true );

        mConnectivityChannelReceiveDecoderManifest = mConnectivityModule->createNewChannel(
            nullptr, config["staticConfig"]["mqttConnection"]["decoderManifestTopic"].asString(), true );

        /*
         * Over this channel metrics like performance (resident ram pages, cpu time spent in threads)
         * and tracing metrics like internal variables and time spent in specially instrumented functions
         * spent are uploaded in json format over mqtt.
         */
        if ( config["staticConfig"]["mqttConnection"]["metricsUploadTopic"].asString().length() > 0 )
        {
            mConnectivityChannelMetricsUpload = mConnectivityModule->createNewChannel(
                nullptr, config["staticConfig"]["mqttConnection"]["metricsUploadTopic"].asString() );
        }
        /*
         * Over this channel log messages that are currently logged to STDOUT are uploaded in json
         * format over MQTT.
         */
        if ( config["staticConfig"]["mqttConnection"]["loggingUploadTopic"].asString().length() > 0 )
        {
            mConnectivityChannelLogsUpload = mConnectivityModule->createNewChannel(
                nullptr, config["staticConfig"]["mqttConnection"]["loggingUploadTopic"].asString() );
        }

        // Create an ISender for sending Checkins
        mConnectivityChannelSendCheckin = mConnectivityModule->createNewChannel(
            nullptr, config["staticConfig"]["mqttConnection"]["checkinTopic"].asString() );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        std::shared_ptr<RawData::BufferManager> rawDataBufferManager;
        boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
        auto rawDataBufferJsonConfig = config["staticConfig"]["visionSystemDataCollection"]["rawDataBuffer"];
        auto rawBufferSize = rawDataBufferJsonConfig.isMember( "maxSize" )
                                 ? boost::make_optional( getJsonValueAsSize( rawDataBufferJsonConfig["maxSize"] ) )
                                 : boost::none;

        if ( rawBufferSize.get_value_or( SIZE_MAX ) > 0 )
        {
            // Create a Raw Data Buffer Manager
            std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
            if ( rawDataBufferJsonConfig.isMember( "overridesPerSignal" ) )
            {
                for ( const auto &signalOverridesJson : rawDataBufferJsonConfig["overridesPerSignal"] )
                {
                    RawData::SignalBufferOverrides signalOverrides;
                    signalOverrides.interfaceId = signalOverridesJson["interfaceId"].asString();
                    signalOverrides.messageId = signalOverridesJson["messageId"].asString();
                    signalOverrides.reservedBytes =
                        signalOverridesJson.isMember( "reservedSize" )
                            ? boost::make_optional( getJsonValueAsSize( signalOverridesJson["reservedSize"] ) )
                            : boost::none;
                    signalOverrides.maxNumOfSamples =
                        signalOverridesJson.isMember( "maxSamples" )
                            ? boost::make_optional( getJsonValueAsSize( signalOverridesJson["maxSamples"] ) )
                            : boost::none;
                    signalOverrides.maxBytesPerSample =
                        signalOverridesJson.isMember( "maxSizePerSample" )
                            ? boost::make_optional( getJsonValueAsSize( signalOverridesJson["maxSizePerSample"] ) )
                            : boost::none;
                    signalOverrides.maxBytes =
                        signalOverridesJson.isMember( "maxSize" )
                            ? boost::make_optional( getJsonValueAsSize( signalOverridesJson["maxSize"] ) )
                            : boost::none;
                    rawDataBufferOverridesPerSignal.emplace_back( signalOverrides );
                }
            }
            rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
                rawBufferSize,
                rawDataBufferJsonConfig.isMember( "reservedSizePerSignal" )
                    ? boost::make_optional( getJsonValueAsSize( rawDataBufferJsonConfig["reservedSizePerSignal"] ) )
                    : boost::none,
                rawDataBufferJsonConfig.isMember( "maxSamplesPerSignal" )
                    ? boost::make_optional( getJsonValueAsSize( rawDataBufferJsonConfig["maxSamplesPerSignal"] ) )
                    : boost::none,
                rawDataBufferJsonConfig.isMember( "maxSizePerSample" )
                    ? boost::make_optional( getJsonValueAsSize( rawDataBufferJsonConfig["maxSizePerSample"] ) )
                    : boost::none,
                rawDataBufferJsonConfig.isMember( "maxSizePerSignal" )
                    ? boost::make_optional( getJsonValueAsSize( rawDataBufferJsonConfig["maxSizePerSignal"] ) )
                    : boost::none,
                rawDataBufferOverridesPerSignal );
            if ( !rawDataBufferManagerConfig )
            {
                FWE_LOG_ERROR( "Failed to create raw data buffer manager config" );
                return false;
            }
            rawDataBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
        }
#endif

        // For asynchronous connect the call needs to be done after all channels created and setTopic calls
        mConnectivityModule->connect();
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
                mConnectivityChannelMetricsUpload,
                mConnectivityChannelLogsUpload,
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
        // Create the Data Inspection Queue
        mCollectedDataReadyToPublish = std::make_shared<CollectedDataReadyToPublish>(
            config["staticConfig"]["internalParameters"]["readyToPublishDataBufferSize"].asInt() );

        // Init and start the Inspection Engine
        mCollectionInspectionWorkerThread = std::make_shared<CollectionInspectionWorkerThread>();
        if ( ( !mCollectionInspectionWorkerThread->init(
                 signalBufferPtr,
                 mCollectedDataReadyToPublish,
                 config["staticConfig"]["threadIdleTimes"]["inspectionThreadIdleTimeMs"].asUInt(),
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                 rawDataBufferManager,
#endif
                 config["staticConfig"]["internalParameters"]["dataReductionProbabilityDisabled"].asBool() ) ) ||
             ( !mCollectionInspectionWorkerThread->start() ) )
        {
            FWE_LOG_ERROR( "Failed to init and start the Inspection Engine" );
            return false;
        }
        /*************************Inspection Engine bootstrap end***********************************/

        /*************************DataSender bootstrap begin*********************************/
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        if ( ( mAwsCredentialsProvider == nullptr ) || ( !config["staticConfig"].isMember( "s3Upload" ) ) )
        {
            FWE_LOG_INFO( "S3 sender not initialized so no vision-system-data data upload will be supported. Add "
                          "'credentialsProvider' and 's3Upload' section to the config to initialize it." )
        }
        else
        {
            auto s3MaxConnections = config["staticConfig"]["s3Upload"]["maxConnections"].asUInt();
            s3MaxConnections = s3MaxConnections > 0U ? s3MaxConnections : 1U;
            auto createTransferManagerWrapper =
                [this, s3MaxConnections]( Aws::Client::ClientConfiguration &clientConfiguration,
                                          Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )
                -> std::shared_ptr<TransferManagerWrapper> {
                clientConfiguration.maxConnections = s3MaxConnections;
                mTransferManagerExecutor =
                    Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>( "executor", 25 );
                transferManagerConfiguration.transferExecutor = mTransferManagerExecutor.get();
                auto s3Client =
                    std::make_shared<Aws::S3::S3Client>( mAwsCredentialsProvider,
                                                         Aws::MakeShared<Aws::S3::S3EndpointProvider>( "S3Client" ),
                                                         clientConfiguration );
                transferManagerConfiguration.s3Client = s3Client;
                return std::make_shared<TransferManagerWrapper>(
                    Aws::Transfer::TransferManager::Create( transferManagerConfiguration ) );
            };
            mS3Sender = std::make_shared<S3Sender>( mPayloadManager,
                                                    createTransferManagerWrapper,
                                                    config["staticConfig"]["s3Upload"]["multipartSize"].asUInt() );
        }
        auto ionWriter = std::make_shared<DataSenderIonWriter>(
            rawDataBufferManager, config["staticConfig"]["mqttConnection"]["clientId"].asString() );
#endif
        mDataSenderManager = std::make_shared<DataSenderManager>(
            mConnectivityChannelSendVehicleData,
            mPayloadManager,
            mCANIDTranslator,
            config["staticConfig"]["publishToCloudParameters"]["maxPublishMessageCount"].asUInt()
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                ,
            mS3Sender,
            ionWriter,
            config["staticConfig"]["mqttConnection"]["clientId"].asString()
#endif
        );
        mDataSenderManagerWorkerThread = std::make_shared<DataSenderManagerWorkerThread>(
            mConnectivityModule, mDataSenderManager, persistencyUploadRetryIntervalMs, mCollectedDataReadyToPublish );
        if ( !mDataSenderManagerWorkerThread->start() )
        {
            FWE_LOG_ERROR( "Failed to init and start the Data Sender" );
            return false;
        }

        if ( !mCollectionInspectionWorkerThread->subscribeListener( mDataSenderManagerWorkerThread.get() ) )
        {
            FWE_LOG_ERROR( "Failed register the Data Sender Thread to the Inspection Module" );
            return false;
        }
        /*************************DataSender bootstrap end*********************************/

        /*************************CollectionScheme Ingestion bootstrap begin*********************************/

        // CollectionScheme Ingestion module executes in the context for the offboardconnectivity thread. Upcoming
        // messages are expected to come either on the decoder manifest topic or the collectionScheme topic or both
        // ( eventually ).
        mSchemaPtr = std::make_shared<Schema>( mConnectivityChannelReceiveDecoderManifest,
                                               mConnectivityChannelReceiveCollectionSchemeList,
                                               mConnectivityChannelSendCheckin );

        /*****************************CollectionScheme Management bootstrap begin*****************************/

        // Create and connect the CollectionScheme Manager
        mCollectionSchemeManagerPtr = std::make_shared<CollectionSchemeManager>();

        if ( !mCollectionSchemeManagerPtr->init(
                 config["staticConfig"]["publishToCloudParameters"]["collectionSchemeManagementCheckinIntervalMs"]
                     .asUInt(),
                 mPersistDecoderManifestCollectionSchemesAndData,
                 mCANIDTranslator
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                 ,
                 rawDataBufferManager
#endif
                 ) )
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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        // Make sure the CollectionScheme Manager can notify the Data Sender about the availability of
        // a new set of collection CollectionSchemes.
        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                 static_cast<IActiveCollectionSchemesListener *>( mDataSenderManagerWorkerThread.get() ) ) )
        {
            FWE_LOG_ERROR( "Failed register the Data Sender to the CollectionScheme Manager Module" );
            return false;
        }
        if ( !mCollectionSchemeManagerPtr->subscribeListener(
                 static_cast<IActiveDecoderDictionaryListener *>( ionWriter.get() ) ) )
        {
            FWE_LOG_ERROR( "Failed register the IonWriter to the CollectionScheme Manager Module" );
            return false;
        }
#endif

        // Allow CollectionSchemeManagement to send checkins through the Schema Object Callback
        mCollectionSchemeManagerPtr->setSchemaListenerPtr( mSchemaPtr );

        /********************************Data source bootstrap start*******************************/

        auto obdOverCANModuleInit = false;
        mCANDataConsumer = std::make_unique<CANDataConsumer>( signalBufferPtr );
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
#ifdef FWE_FEATURE_ROS2
            else if ( interfaceType == ROS2_INTERFACE_TYPE )
            {
                ROS2DataSourceConfig ros2Config;
                if ( !ROS2DataSourceConfig::parseFromJson( interfaceName, ros2Config ) )
                {
                    FWE_LOG_ERROR( "Could not parse ros2Interface configuration" );
                    return false;
                }
                mROS2DataSource = std::make_shared<ROS2DataSource>( ros2Config, signalBufferPtr, rawDataBufferManager );
                mROS2DataSource->connect();
                if ( !mCollectionSchemeManagerPtr->subscribeListener(
                         static_cast<IActiveDecoderDictionaryListener *>( mROS2DataSource.get() ) ) )
                {
                    FWE_LOG_ERROR( "Failed register the ROS2 Data Source to the CollectionScheme Manager Module" );
                    return false;
                }
            }
#endif
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

#ifdef FWE_FEATURE_IWAVE_GPS
        /********************************IWave GPS Example NMEA reader *********************************/
        mIWaveGpsSource = std::make_shared<IWaveGpsSource>( signalBufferPtr );
        std::string pathToNmeaSource;
        CANChannelNumericID canChannel{};
        CANRawFrameID canRawFrameId{};
        uint16_t latitudeStartBit{};
        uint16_t longitudeStartBit{};
        if ( config["staticConfig"].isMember( "iWaveGpsExample" ) )
        {
            FWE_LOG_TRACE( "Found 'iWaveGpsExample' section in config file" );
            pathToNmeaSource = config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::PATH_TO_NMEA].asString();
            canChannel = mCANIDTranslator.getChannelNumericID(
                config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::CAN_CHANNEL_NUMBER].asString() );
            canRawFrameId =
                stringToU32( config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::CAN_RAW_FRAME_ID].asString() );
            latitudeStartBit = static_cast<uint16_t>( stringToU32(
                config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::LATITUDE_START_BIT].asString() ) );
            longitudeStartBit = static_cast<uint16_t>( stringToU32(
                config["staticConfig"]["iWaveGpsExample"][IWaveGpsSource::LONGITUDE_START_BIT].asString() ) );
        }
        else
        {
            // If not config available, autodetect the presence of the iWave by passing a blank source:
            pathToNmeaSource = "";
            canChannel = mCANIDTranslator.getChannelNumericID( "IWAVE-GPS-CAN" );
            // Default to these values:
            canRawFrameId = 1;
            latitudeStartBit = 32;
            longitudeStartBit = 0;
        }
        if ( mIWaveGpsSource->init( pathToNmeaSource, canChannel, canRawFrameId, latitudeStartBit, longitudeStartBit ) )
        {
            if ( !mIWaveGpsSource->connect() )
            {
                FWE_LOG_ERROR( "IWaveGps initialization failed" );
                return false;
            }
            if ( !mCollectionSchemeManagerPtr->subscribeListener(
                     static_cast<IActiveDecoderDictionaryListener *>( mIWaveGpsSource.get() ) ) )
            {
                FWE_LOG_ERROR( "Failed to register the IWaveGps to the CollectionScheme Manager" );
                return false;
            }
            mIWaveGpsSource->start();
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

#ifdef FWE_FEATURE_AAOS_VHAL
        /********************************AAOS VHAL Example reader *********************************/
        mAaosVhalSource = std::make_shared<AaosVhalSource>( signalBufferPtr );
        bool aaosVhalInitSuccessful = false;
        if ( config["staticConfig"].isMember( "aaosVhalExample" ) )
        {
            FWE_LOG_TRACE( "Found 'aaosVhalExample' section in config file" );
            aaosVhalInitSuccessful = mAaosVhalSource->init(
                mCANIDTranslator.getChannelNumericID(
                    config["staticConfig"]["aaosVhalExample"][AaosVhalSource::CAN_CHANNEL_NUMBER].asString() ),
                stringToU32( config["staticConfig"]["aaosVhalExample"][AaosVhalSource::CAN_RAW_FRAME_ID].asString() ) );
        }
        else
        {
            // If not config available default to this values
            aaosVhalInitSuccessful =
                mAaosVhalSource->init( mCANIDTranslator.getChannelNumericID( "AAOS-VHAL-CAN" ), 1 );
        }
        if ( aaosVhalInitSuccessful )
        {
            if ( !mCollectionSchemeManagerPtr->subscribeListener(
                     static_cast<IActiveDecoderDictionaryListener *>( mAaosVhalSource.get() ) ) )
            {
                FWE_LOG_ERROR( "Failed to register the AaosVhalExample to the CollectionScheme Manager" );
                return false;
            }
            mAaosVhalSource->start();
        }
        else
        {
            FWE_LOG_ERROR( "AaosVhalExample initialization failed" );
            return false;
        }
        /********************************AAOS VHAL Example reader end******************************/
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
#ifdef FWE_FEATURE_AAOS_VHAL
    if ( mAaosVhalSource )
    {
        mAaosVhalSource->stop();
    }
#endif
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
#ifdef FWE_FEATURE_ROS2
    if ( mROS2DataSource )
    {
        mROS2DataSource->disconnect();
    }
#endif

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

    if ( mConnectivityModule->isAlive() && ( !mConnectivityModule->disconnect() ) )
    {
        FWE_LOG_ERROR( "Could not disconnect the offboard connectivity" );
        return false;
    }

    if ( !mDataSenderManagerWorkerThread->stop() )
    {
        FWE_LOG_ERROR( "Could not stop the DataSenderManager" );
        return false;
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    if ( mS3Sender != nullptr )
    {
        if ( !mS3Sender->disconnect() )
        {
            FWE_LOG_ERROR( "Could not disconnect the S3Sender" );
            return false;
        }
    }
#endif

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
    TraceModule::get().sectionEnd( TraceSection::FWE_STARTUP );

    while ( !engine->shouldStop() )
    {
        engine->mTimer.reset();
        uint64_t minTimeToWaitMs = UINT64_MAX;
        if ( engine->mPrintMetricsCyclicPeriodMs != 0 )
        {
            uint64_t timeToWaitMs =
                engine->mPrintMetricsCyclicPeriodMs -
                std::min( static_cast<uint64_t>( engine->mPrintMetricsCyclicTimer.getElapsedMs().count() ),
                          engine->mPrintMetricsCyclicPeriodMs );
            minTimeToWaitMs = std::min( minTimeToWaitMs, timeToWaitMs );
        }
        if ( minTimeToWaitMs < UINT64_MAX )
        {
            FWE_LOG_TRACE( "Waiting for: " + std::to_string( minTimeToWaitMs ) + " ms. Cyclic metrics print:" +
                           std::to_string( engine->mPrintMetricsCyclicPeriodMs ) + " configured,  " +
                           std::to_string( engine->mPrintMetricsCyclicTimer.getElapsedMs().count() ) + " timer." );
            engine->mWait.wait( static_cast<uint32_t>( minTimeToWaitMs ) );
        }
        else
        {
            engine->mWait.wait( Signal::WaitWithPredicate );
            auto elapsedTimeMs = engine->mTimer.getElapsedMs().count();
            FWE_LOG_TRACE( "Event arrived. Time elapsed waiting for the event: " + std::to_string( elapsedTimeMs ) +
                           " ms" );
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

#ifdef FWE_FEATURE_AAOS_VHAL
std::vector<std::array<uint32_t, 4>>
IoTFleetWiseEngine::getVehiclePropertyInfo()
{
    std::vector<std::array<uint32_t, 4>> propertyInfo;
    if ( mAaosVhalSource != nullptr )
    {
        propertyInfo = mAaosVhalSource->getVehiclePropertyInfo();
    }
    return propertyInfo;
}
void
IoTFleetWiseEngine::setVehicleProperty( uint32_t signalId, double value )
{
    if ( mAaosVhalSource == nullptr )
    {
        return;
    }
    mAaosVhalSource->setVehicleProperty( signalId, value );
}
#endif

std::string
IoTFleetWiseEngine::getStatusSummary()
{
    if ( mConnectivityModule == nullptr || mCollectionSchemeManagerPtr == nullptr ||
         mConnectivityChannelSendVehicleData == nullptr || mOBDOverCANModule == nullptr )
    {
        return "";
    }
    std::string status;
    status += std::string( "MQTT connection: " ) + ( mConnectivityModule->isAlive() ? "CONNECTED" : "NOT CONNECTED" ) +
              "\n\n";

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

    status += "Payloads sent: " + std::to_string( mConnectivityChannelSendVehicleData->getPayloadCountSent() ) + "\n\n";
    return status;
}

} // namespace IoTFleetWise
} // namespace Aws
