// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IoTFleetWiseEngine.h"
#include "AwsBootstrap.h"
#include "AwsIotConnectivityModule.h"
#include "AwsSDKMemoryManager.h"
#include "CollectionInspectionAPITypes.h"
#include "DataSenderManager.h"
#include "DataSenderProtoWriter.h"
#include "DataSenderTypes.h"
#include "ILogger.h"
#include "IoTFleetWiseConfig.h"
#include "LogLevel.h"
#include "LoggingModule.h"
#include "MqttClientWrapper.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "TelemetryDataSender.h"
#include "TraceModule.h"
#include <algorithm>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/iot/Mqtt5Client.h>
#include <boost/optional/optional.hpp>
#include <cstddef>
#include <exception>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <utility>

#ifdef FWE_FEATURE_GREENGRASSV2
#include "AwsGreengrassV2ConnectivityModule.h"
#ifdef FWE_FEATURE_S3
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#endif
#endif
#ifdef FWE_FEATURE_S3
#include "Credentials.h"
#include "TransferManagerWrapper.h"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/S3ServiceClientModel.h>
#include <aws/transfer/TransferManager.h>
#endif
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "DataSenderIonWriter.h"
#include "VisionSystemDataSender.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

static constexpr uint64_t DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS = 10000;
static const std::string CAN_INTERFACE_TYPE = "canInterface";
static const std::string EXTERNAL_CAN_INTERFACE_TYPE = "externalCanInterface";
static const std::string OBD_INTERFACE_TYPE = "obdInterface";
#ifdef FWE_FEATURE_ROS2
static const std::string ROS2_INTERFACE_TYPE = "ros2Interface";
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
static const std::string CONFIG_SECTION_IWAVE_GPS = "iWaveGpsExample";
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
static const std::string CONFIG_SECTION_EXTERNAL_GPS = "externalGpsExample";
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
static const std::string CONFIG_SECTION_AAOS_VHAL = "aaosVhalExample";
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

/**
 * @brief Get the absolute file path, if the path is already absolute its returned.
 *
 * @param p The file path
 * @param basePath Base path to which the p is relative
 * @return boost::filesystem::path Absolute file path
 */
boost::filesystem::path
getAbsolutePath( const std::string &p, const boost::filesystem::path &basePath )
{
    boost::filesystem::path filePath( p );
    if ( !filePath.is_absolute() )
    {
        return basePath / filePath;
    }
    return filePath;
}

} // namespace

#ifdef FWE_FEATURE_S3
std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor>
IoTFleetWiseEngine::getTransferManagerExecutor()
{
    std::lock_guard<std::mutex> lock( mTransferManagerExecutorMutex );
    if ( mTransferManagerExecutor == nullptr )
    {
        mTransferManagerExecutor = Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>( "executor", 25 );
    }
    return mTransferManagerExecutor;
}
#endif

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
IoTFleetWiseEngine::connect( const Json::Value &jsonConfig, const boost::filesystem::path &configFileDirectoryPath )
{
    // Main bootstrap sequence.
    try
    {
        IoTFleetWiseConfig config( jsonConfig );
        uint64_t persistencyUploadRetryIntervalMs = 0;
        if ( ( config["staticConfig"].isMember( "persistency" ) ) )
        {
            const auto persistencyPath = config["staticConfig"]["persistency"]["persistencyPath"].asStringRequired();
            /*************************Payload Manager and Persistency library bootstrap begin*********/
            // Create an object for Persistency
            mPersistDecoderManifestCollectionSchemesAndData = std::make_shared<CacheAndPersist>(
                getAbsolutePath( persistencyPath, configFileDirectoryPath ).string(),
                config["staticConfig"]["persistency"]["persistencyPartitionMaxSize"].asSizeRequired() );
            if ( !mPersistDecoderManifestCollectionSchemesAndData->init() )
            {
                FWE_LOG_ERROR( "Failed to init persistency library" );
            }
            persistencyUploadRetryIntervalMs =
                config["staticConfig"]["persistency"]["persistencyUploadRetryIntervalMs"].asU64Optional().get_value_or(
                    DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS );
            // Payload Manager for offline data management
            mPayloadManager = std::make_shared<PayloadManager>( mPersistDecoderManifestCollectionSchemesAndData );
        }
        else
        {
            FWE_LOG_INFO( "Persistency feature is disabled in the configuration." );
        }
        /*************************Payload Manager and Persistency library bootstrap end************/

        /*************************CAN InterfaceID to InternalID Translator begin*********/
        for ( unsigned i = 0; i < config["networkInterfaces"].getArraySizeRequired(); i++ )
        {
            auto networkInterface = config["networkInterfaces"][i];
            auto networkInterfaceType = networkInterface["type"].asStringRequired();
            if ( ( networkInterfaceType == CAN_INTERFACE_TYPE ) ||
                 ( networkInterfaceType == EXTERNAL_CAN_INTERFACE_TYPE ) )
            {
                mCANIDTranslator.add( networkInterface["interfaceId"].asStringRequired() );
            }
        }
#ifdef FWE_FEATURE_IWAVE_GPS
        if ( config["staticConfig"].isMember( CONFIG_SECTION_IWAVE_GPS ) )
        {
            mCANIDTranslator.add( config["staticConfig"][CONFIG_SECTION_IWAVE_GPS][IWaveGpsSource::CAN_CHANNEL_NUMBER]
                                      .asStringRequired() );
        }
        else
        {
            mCANIDTranslator.add( "IWAVE-GPS-CAN" );
        }
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
        if ( config["staticConfig"].isMember( CONFIG_SECTION_EXTERNAL_GPS ) )
        {
            mCANIDTranslator.add(
                config["staticConfig"][CONFIG_SECTION_EXTERNAL_GPS][ExternalGpsSource::CAN_CHANNEL_NUMBER]
                    .asStringRequired() );
        }
        else
        {
            mCANIDTranslator.add( "EXTERNAL-GPS-CAN" );
        }
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
        if ( config["staticConfig"].isMember( CONFIG_SECTION_AAOS_VHAL ) )
        {
            mCANIDTranslator.add( config["staticConfig"][CONFIG_SECTION_AAOS_VHAL][AaosVhalSource::CAN_CHANNEL_NUMBER]
                                      .asStringRequired() );
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
                config["staticConfig"]["internalParameters"]["maximumAwsSdkHeapMemoryBytes"].asSizeRequired();
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

        auto mqttConfig = config["staticConfig"]["mqttConnection"];
        auto clientId = mqttConfig["clientId"].asStringRequired();
        std::string connectionType = mqttConfig["connectionType"].asStringOptional().get_value_or( "iotCore" );

        if ( connectionType == "iotCore" )
        {
            std::string privateKey;
            std::string certificate;
            std::string rootCA;
            FWE_LOG_INFO( "ConnectionType is iotCore" );
            // fetch connection parameters from config
            if ( mqttConfig.isMember( "privateKey" ) )
            {
                privateKey = mqttConfig["privateKey"].asStringRequired();
            }
            else if ( mqttConfig.isMember( "privateKeyFilename" ) )
            {
                auto privKeyPathAbs =
                    getAbsolutePath( mqttConfig["privateKeyFilename"].asStringRequired(), configFileDirectoryPath )
                        .string();
                privateKey = getFileContents( privKeyPathAbs );
            }
            if ( mqttConfig.isMember( "certificate" ) )
            {
                certificate = mqttConfig["certificate"].asStringRequired();
            }
            else if ( mqttConfig.isMember( "certificateFilename" ) )
            {
                auto certPathAbs =
                    getAbsolutePath( mqttConfig["certificateFilename"].asStringRequired(), configFileDirectoryPath )
                        .string();
                certificate = getFileContents( certPathAbs );
            }
            if ( mqttConfig.isMember( "rootCA" ) )
            {
                rootCA = mqttConfig["rootCA"].asStringRequired();
            }
            else if ( mqttConfig.isMember( "rootCAFilename" ) )
            {
                auto rootCAPathAbs =
                    getAbsolutePath( mqttConfig["rootCAFilename"].asStringRequired(), configFileDirectoryPath )
                        .string();
                rootCA = getFileContents( rootCAPathAbs );
            }
            // coverity[autosar_cpp14_a20_8_5_violation] - can't use make_unique as the constructor is private
            auto builder = std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder>(
                Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromMemory(
                    mqttConfig["endpointUrl"].asStringRequired().c_str(),
                    Crt::ByteCursorFromCString( certificate.c_str() ),
                    Crt::ByteCursorFromCString( privateKey.c_str() ) ) );

            std::unique_ptr<MqttClientBuilderWrapper> builderWrapper;
            if ( builder == nullptr )
            {
                FWE_LOG_ERROR( "Failed to setup mqtt5 client builder with error code " +
                               std::to_string( Aws::Crt::LastError() ) + ": " +
                               Aws::Crt::ErrorDebugString( Aws::Crt::LastError() ) );
                return false;
            }
            else
            {
                builder->WithBootstrap( bootstrapPtr );
                builderWrapper = std::make_unique<MqttClientBuilderWrapper>( std::move( builder ) );
            }

            AwsIotConnectivityConfig mqttConnectionConfig;
            mqttConnectionConfig.keepAliveIntervalSeconds =
                static_cast<uint16_t>( mqttConfig["keepAliveIntervalSeconds"].asU32Optional().get_value_or(
                    MQTT_KEEP_ALIVE_INTERVAL_SECONDS ) );
            mqttConnectionConfig.pingTimeoutMs =
                mqttConfig["pingTimeoutMs"].asU32Optional().get_value_or( MQTT_PING_TIMEOUT_MS );
            mqttConnectionConfig.sessionExpiryIntervalSeconds =
                mqttConfig["sessionExpiryIntervalSeconds"].asU32Optional().get_value_or(
                    MQTT_SESSION_EXPIRY_INTERVAL_SECONDS );

            mConnectivityModule = std::make_shared<AwsIotConnectivityModule>(
                rootCA, clientId, std::move( builderWrapper ), mqttConnectionConfig );

#ifdef FWE_FEATURE_S3
            if ( config["staticConfig"].isMember( "credentialsProvider" ) )
            {
                auto crtCredentialsProvider = createX509CredentialsProvider(
                    bootstrapPtr,
                    clientId,
                    privateKey,
                    certificate,
                    config["staticConfig"]["credentialsProvider"]["endpointUrl"].asStringRequired(),
                    config["staticConfig"]["credentialsProvider"]["roleAlias"].asStringRequired() );
                mAwsCredentialsProvider = std::make_shared<CrtCredentialsProviderAdapter>( crtCredentialsProvider );
            }
#endif
        }
#ifdef FWE_FEATURE_GREENGRASSV2
        else if ( connectionType == "iotGreengrassV2" )
        {
            FWE_LOG_INFO( "ConnectionType is iotGreengrassV2" );
            mConnectivityModule = std::make_shared<AwsGreengrassV2ConnectivityModule>( bootstrapPtr );
#ifdef FWE_FEATURE_S3
            mAwsCredentialsProvider = std::make_shared<Aws::Auth::DefaultAWSCredentialsProviderChain>();
#endif
        }
#endif
        else
        {
            FWE_LOG_ERROR( "Unknown connection type: " + connectionType );
            return false;
        }

        // Only CAN data channel needs a payloadManager object for persistency and compression support,
        // for other components this will be nullptr
        mSenderVehicleData = mConnectivityModule->createSender( mqttConfig["canDataTopic"].asStringRequired() );

        mReceiverCollectionSchemeList =
            mConnectivityModule->createReceiver( mqttConfig["collectionSchemeListTopic"].asStringRequired() );

        mReceiverDecoderManifest =
            mConnectivityModule->createReceiver( mqttConfig["decoderManifestTopic"].asStringRequired() );

        /*
         * Over this sender, metrics like performance (resident ram pages, cpu time spent in threads)
         * and tracing metrics like internal variables and time spent in specially instrumented functions
         * spent are uploaded in json format over mqtt.
         */
        auto metricsUploadTopic = mqttConfig["metricsUploadTopic"].asStringOptional().get_value_or( "" );
        if ( !metricsUploadTopic.empty() )
        {
            mSenderMetrics = mConnectivityModule->createSender( metricsUploadTopic );
        }
        /*
         * Over this sender, log messages that are currently logged to STDOUT are uploaded in json
         * format over MQTT.
         */
        auto loggingUploadTopic = mqttConfig["loggingUploadTopic"].asStringOptional().get_value_or( "" );
        if ( !loggingUploadTopic.empty() )
        {
            mSenderLogs = mConnectivityModule->createSender( loggingUploadTopic );
        }

        // Create an ISender for sending Checkins
        mSenderCheckin = mConnectivityModule->createSender( mqttConfig["checkinTopic"].asStringRequired() );

        boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
        auto rawDataBufferJsonConfig = config["staticConfig"]["visionSystemDataCollection"]["rawDataBuffer"];
        auto rawBufferSize = rawDataBufferJsonConfig["maxSize"].asSizeOptional();

        if ( rawBufferSize.get_value_or( SIZE_MAX ) > 0 )
        {
            // Create a Raw Data Buffer Manager
            std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
            for ( auto i = 0U; i < rawDataBufferJsonConfig["overridesPerSignal"].getArraySizeOptional(); i++ )
            {
                auto signalOverridesJson = rawDataBufferJsonConfig["overridesPerSignal"][i];
                RawData::SignalBufferOverrides signalOverrides;
                signalOverrides.interfaceId = signalOverridesJson["interfaceId"].asStringRequired();
                signalOverrides.messageId = signalOverridesJson["messageId"].asStringRequired();
                signalOverrides.reservedBytes = signalOverridesJson["reservedSize"].asSizeOptional();
                signalOverrides.maxNumOfSamples = signalOverridesJson["maxSamples"].asSizeOptional();
                signalOverrides.maxBytesPerSample = signalOverridesJson["maxSizePerSample"].asSizeOptional();
                signalOverrides.maxBytes = signalOverridesJson["maxSize"].asSizeOptional();
                rawDataBufferOverridesPerSignal.emplace_back( signalOverrides );
            }
            rawDataBufferManagerConfig =
                RawData::BufferManagerConfig::create( rawBufferSize,
                                                      rawDataBufferJsonConfig["reservedSizePerSignal"].asSizeOptional(),
                                                      rawDataBufferJsonConfig["maxSamplesPerSignal"].asSizeOptional(),
                                                      rawDataBufferJsonConfig["maxSizePerSample"].asSizeOptional(),
                                                      rawDataBufferJsonConfig["maxSizePerSignal"].asSizeOptional(),
                                                      rawDataBufferOverridesPerSignal );
            if ( !rawDataBufferManagerConfig )
            {
                FWE_LOG_ERROR( "Failed to create raw data buffer manager config" );
                return false;
            }
            mRawBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
        }

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
                config["staticConfig"]["remoteProfilerDefaultValues"]["loggingUploadLevelThreshold"].asStringRequired(),
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
                mSenderMetrics,
                mSenderLogs,
                config["staticConfig"]["remoteProfilerDefaultValues"]["metricsUploadIntervalMs"].asU32Required(),
                config["staticConfig"]["remoteProfilerDefaultValues"]["loggingUploadMaxWaitBeforeUploadMs"]
                    .asU32Required(),
                logThreshold,
                config["staticConfig"]["remoteProfilerDefaultValues"]["profilerPrefix"].asStringRequired() );
            setLogForwarding( mRemoteProfiler.get() );
        }
        /*************************Remote Profiling bootstrap ends**********************************/

        /*************************Inspection Engine bootstrap begin*********************************/

        auto signalBufferSize = config["staticConfig"]["bufferSizes"]["decodedSignalsBufferSize"].asSizeRequired();
        auto signalBuffer =
            std::make_shared<SignalBuffer>( signalBufferSize,
                                            "Signal Buffer",
                                            TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_DATA_FRAMES,
                                            // Notify listeners when 10% of the buffer is full so that we don't
                                            // let it grow too much.
                                            signalBufferSize / 10 );

        auto signalBufferDistributor = std::make_shared<SignalBufferDistributor>();
        signalBufferDistributor->registerQueue( signalBuffer );

        // Create the Data Inspection Queue
        mCollectedDataReadyToPublish = std::make_shared<DataSenderQueue>(
            config["staticConfig"]["internalParameters"]["readyToPublishDataBufferSize"].asSizeRequired(),
            "Collected Data",
            TraceAtomicVariable::QUEUE_INSPECTION_TO_SENDER );

        auto dataSenderProtoWriter = std::make_shared<DataSenderProtoWriter>( mCANIDTranslator, mRawBufferManager );
        auto payloadConfigUncompressed = config["staticConfig"]["payloadAdaption"]["uncompressed"];
        PayloadAdaptionConfig payloadAdaptionConfigUncompressed{
            payloadConfigUncompressed["transmitThresholdStartPercent"].asU32Optional().get_value_or( 80 ),
            payloadConfigUncompressed["payloadSizeLimitMinPercent"].asU32Optional().get_value_or( 70 ),
            payloadConfigUncompressed["payloadSizeLimitMaxPercent"].asU32Optional().get_value_or( 90 ),
            payloadConfigUncompressed["transmitThresholdAdaptPercent"].asU32Optional().get_value_or( 10 ) };
        auto payloadConfigCompressed = config["staticConfig"]["payloadAdaption"]["compressed"];
        // Snappy typically compresses to around 30% of original size, so set the starting compressed transmit threshold
        // to double the maximum payload size:
        PayloadAdaptionConfig payloadAdaptionConfigCompressed{
            payloadConfigCompressed["transmitThresholdStartPercent"].asU32Optional().get_value_or( 200 ),
            payloadConfigCompressed["payloadSizeLimitMinPercent"].asU32Optional().get_value_or( 70 ),
            payloadConfigCompressed["payloadSizeLimitMaxPercent"].asU32Optional().get_value_or( 90 ),
            payloadConfigCompressed["transmitThresholdAdaptPercent"].asU32Optional().get_value_or( 10 ) };
        auto telemetryDataSender = std::make_shared<TelemetryDataSender>( mSenderVehicleData,
                                                                          dataSenderProtoWriter,
                                                                          payloadAdaptionConfigUncompressed,
                                                                          payloadAdaptionConfigCompressed );
        std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders;
        dataSenders[SenderDataType::TELEMETRY] = telemetryDataSender;

        // Init and start the Inspection Engine
        mCollectionInspectionEngine = std::make_shared<CollectionInspectionEngine>();
        mCollectionInspectionWorkerThread =
            std::make_shared<CollectionInspectionWorkerThread>( *mCollectionInspectionEngine );
        if ( ( !mCollectionInspectionWorkerThread->init(
                 signalBuffer,
                 mCollectedDataReadyToPublish,
                 config["staticConfig"]["threadIdleTimes"]["inspectionThreadIdleTimeMs"].asU32Required(),
                 mRawBufferManager ) ) ||
             ( !mCollectionInspectionWorkerThread->start() ) )
        {
            FWE_LOG_ERROR( "Failed to init and start the Inspection Engine" );
            return false;
        }
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        signalBuffer->subscribeToNewDataAvailable( std::bind( &CollectionInspectionWorkerThread::onNewDataAvailable,
                                                              mCollectionInspectionWorkerThread.get() ) );
        /*************************Inspection Engine bootstrap end***********************************/

        /*************************DataSender bootstrap begin*********************************/
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        std::shared_ptr<DataSenderIonWriter> ionWriter;
        std::shared_ptr<VisionSystemDataSender> visionSystemDataSender;
        if ( ( mAwsCredentialsProvider == nullptr ) || ( !config["staticConfig"].isMember( "s3Upload" ) ) )
        {
            FWE_LOG_INFO( "S3 sender not initialized so no vision-system-data data upload will be supported. Add "
                          "'credentialsProvider' and 's3Upload' section to the config to initialize it." )
        }
        else
        {
            auto s3MaxConnections = config["staticConfig"]["s3Upload"]["maxConnections"].asU32Required();
            s3MaxConnections = s3MaxConnections > 0U ? s3MaxConnections : 1U;
            auto createTransferManagerWrapper =
                [this, s3MaxConnections]( Aws::Client::ClientConfiguration &clientConfiguration,
                                          Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )
                -> std::shared_ptr<TransferManagerWrapper> {
                clientConfiguration.maxConnections = s3MaxConnections;
                transferManagerConfiguration.transferExecutor = getTransferManagerExecutor().get();
                auto s3Client =
                    std::make_shared<Aws::S3::S3Client>( mAwsCredentialsProvider,
                                                         Aws::MakeShared<Aws::S3::S3EndpointProvider>( "S3Client" ),
                                                         clientConfiguration );
                transferManagerConfiguration.s3Client = s3Client;
                return std::make_shared<TransferManagerWrapper>(
                    Aws::Transfer::TransferManager::Create( transferManagerConfiguration ) );
            };
            mS3Sender = std::make_shared<S3Sender>(
                createTransferManagerWrapper, config["staticConfig"]["s3Upload"]["multipartSize"].asSizeRequired() );
            ionWriter = std::make_shared<DataSenderIonWriter>( mRawBufferManager, clientId );
            visionSystemDataSender = std::make_shared<VisionSystemDataSender>(
                mCollectedDataReadyToPublish, mS3Sender, ionWriter, clientId );
            dataSenders[SenderDataType::VISION_SYSTEM] = visionSystemDataSender;
        }
#endif

        mDataSenderManager =
            std::make_shared<DataSenderManager>( std::move( dataSenders ), mSenderVehicleData, mPayloadManager );
        std::vector<std::shared_ptr<DataSenderQueue>> dataToSendQueues = { mCollectedDataReadyToPublish };
        mDataSenderManagerWorkerThread = std::make_shared<DataSenderManagerWorkerThread>(
            mConnectivityModule, mDataSenderManager, persistencyUploadRetryIntervalMs, dataToSendQueues );
        if ( !mDataSenderManagerWorkerThread->start() )
        {
            FWE_LOG_ERROR( "Failed to init and start the Data Sender" );
            return false;
        }

        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mCollectedDataReadyToPublish->subscribeToNewDataAvailable(
            std::bind( &DataSenderManagerWorkerThread::onDataReadyToPublish, mDataSenderManagerWorkerThread.get() ) );
        /*************************DataSender bootstrap end*********************************/

        /*************************CollectionScheme Ingestion bootstrap begin*********************************/

        // CollectionScheme Ingestion module executes in the context for the offboardconnectivity thread. Upcoming
        // messages are expected to come either on the decoder manifest topic or the collectionScheme topic or both
        // ( eventually ).
        mSchemaPtr =
            std::make_shared<Schema>( mReceiverDecoderManifest, mReceiverCollectionSchemeList, mSenderCheckin );

        /*****************************CollectionScheme Management bootstrap begin*****************************/

        // Allow CollectionSchemeManagement to send checkins through the Schema Object Callback
        mCheckinSender = std::make_shared<CheckinSender>(
            mSchemaPtr,
            config["staticConfig"]["publishToCloudParameters"]["collectionSchemeManagementCheckinIntervalMs"]
                .asU32Required() );

        // Create and connect the CollectionScheme Manager
        mCollectionSchemeManagerPtr = std::make_shared<CollectionSchemeManager>(
            mPersistDecoderManifestCollectionSchemesAndData, mCANIDTranslator, mCheckinSender, mRawBufferManager );

        // Make sure the CollectionScheme Ingestion can notify the CollectionScheme Manager about the arrival
        // of new artifacts over the offboardconnectivity receiver.
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mSchemaPtr->subscribeToCollectionSchemeUpdate( std::bind( &CollectionSchemeManager::onCollectionSchemeUpdate,
                                                                  mCollectionSchemeManagerPtr.get(),
                                                                  std::placeholders::_1 ) );
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mSchemaPtr->subscribeToDecoderManifestUpdate( std::bind( &CollectionSchemeManager::onDecoderManifestUpdate,
                                                                 mCollectionSchemeManagerPtr.get(),
                                                                 std::placeholders::_1 ) );

        // Make sure the CollectionScheme Manager can notify the Inspection Engine about the availability of
        // a new set of collection CollectionSchemes.
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mCollectionSchemeManagerPtr->subscribeToInspectionMatrixChange(
            std::bind( &CollectionInspectionWorkerThread::onChangeInspectionMatrix,
                       mCollectionInspectionWorkerThread.get(),
                       std::placeholders::_1 ) );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        // Make sure the CollectionScheme Manager can notify the Data Sender about the availability of
        // a new set of collection CollectionSchemes.
        if ( visionSystemDataSender != nullptr )
        {
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToCollectionSchemeListChange(
                std::bind( &VisionSystemDataSender::onChangeCollectionSchemeList,
                           visionSystemDataSender.get(),
                           std::placeholders::_1 ) );
        }

        if ( ionWriter != nullptr )
        {
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                std::bind( &DataSenderIonWriter::onChangeOfActiveDictionary,
                           ionWriter.get(),
                           std::placeholders::_1,
                           std::placeholders::_2 ) );
        }
#endif

        /********************************Data source bootstrap start*******************************/

        auto obdOverCANModuleInit = false;
        mCANDataConsumer = std::make_unique<CANDataConsumer>( signalBufferDistributor );
        for ( unsigned i = 0; i < config["networkInterfaces"].getArraySizeRequired(); i++ )
        {
            const auto networkInterfaceConfig = config["networkInterfaces"][i];
            const auto interfaceType = networkInterfaceConfig["type"].asStringRequired();
            const auto interfaceId = networkInterfaceConfig["interfaceId"].asStringRequired();

            if ( interfaceType == CAN_INTERFACE_TYPE )
            {
                CanTimestampType canTimestampType = CanTimestampType::KERNEL_SOFTWARE_TIMESTAMP; // default
                auto canConfig = networkInterfaceConfig[CAN_INTERFACE_TYPE];
                if ( canConfig.isMember( "timestampType" ) )
                {
                    auto timestampTypeInput = canConfig["timestampType"].asStringRequired();
                    bool success = stringToCanTimestampType( timestampTypeInput, canTimestampType );
                    if ( !success )
                    {
                        FWE_LOG_WARN( "Invalid can timestamp type provided: " + timestampTypeInput +
                                      " so default to Software" );
                    }
                }
                auto canChannelId = mCANIDTranslator.getChannelNumericID( interfaceId );
                auto canSourcePtr = std::make_unique<CANDataSource>(
                    canChannelId,
                    canTimestampType,
                    canConfig["interfaceName"].asStringRequired(),
                    canConfig["protocolName"].asStringRequired() == "CAN-FD",
                    config["staticConfig"]["threadIdleTimes"]["socketCANThreadIdleTimeMs"].asU32Required(),
                    *mCANDataConsumer );
                if ( !canSourcePtr->init() )
                {
                    FWE_LOG_ERROR( "Failed to initialize CANDataSource" );
                    return false;
                }
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &CANDataSource::onChangeOfActiveDictionary,
                               canSourcePtr.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
                mCANDataSources.push_back( std::move( canSourcePtr ) );
            }
            else if ( interfaceType == OBD_INTERFACE_TYPE )
            {
                if ( !obdOverCANModuleInit )
                {
                    auto obdOverCANModule = std::make_shared<OBDOverCANModule>();
                    obdOverCANModuleInit = true;
                    auto obdConfig = networkInterfaceConfig[OBD_INTERFACE_TYPE];
                    if ( obdOverCANModule->init(
                             signalBufferDistributor,
                             obdConfig["interfaceName"].asStringRequired(),
                             obdConfig["pidRequestIntervalSeconds"].asU32Required(),
                             obdConfig["dtcRequestIntervalSeconds"].asU32Required(),
                             // Broadcast mode is enabled by default if not defined in config:
                             obdConfig["broadcastRequests"].asBoolOptional().get_value_or( true ) ) )
                    {
                        // Connect the OBD Module
                        mOBDOverCANModule = obdOverCANModule;
                        if ( !mOBDOverCANModule->connect() )
                        {
                            FWE_LOG_ERROR( "Failed to connect OBD over CAN module" );
                            return false;
                        }

                        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                        mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                            std::bind( &OBDOverCANModule::onChangeOfActiveDictionary,
                                       mOBDOverCANModule.get(),
                                       std::placeholders::_1,
                                       std::placeholders::_2 ) );
                        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                        mCollectionSchemeManagerPtr->subscribeToInspectionMatrixChange(
                            std::bind( &OBDOverCANModule::onChangeInspectionMatrix,
                                       mOBDOverCANModule.get(),
                                       std::placeholders::_1 ) );
                    }
                }
                else
                {
                    FWE_LOG_ERROR( "obdOverCANModule already initialised" );
                }
            }
            else if ( interfaceType == EXTERNAL_CAN_INTERFACE_TYPE )
            {
                if ( mExternalCANDataSource != nullptr )
                {
                    continue;
                }
                mExternalCANDataSource = std::make_unique<ExternalCANDataSource>( *mCANDataConsumer );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &ExternalCANDataSource::onChangeOfActiveDictionary,
                               mExternalCANDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
            }
#ifdef FWE_FEATURE_ROS2
            else if ( interfaceType == ROS2_INTERFACE_TYPE )
            {
                ROS2DataSourceConfig ros2Config;
                if ( !ROS2DataSourceConfig::parseFromJson( networkInterfaceConfig, ros2Config ) )
                {
                    return false;
                }
                mROS2DataSource =
                    std::make_shared<ROS2DataSource>( ros2Config, signalBufferDistributor, mRawBufferManager );
                mROS2DataSource->connect();
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &ROS2DataSource::onChangeOfActiveDictionary,
                               mROS2DataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
            }
#endif
            else
            {
                FWE_LOG_ERROR( interfaceType + " is not supported" );
            }
        }

        /********************************Data source bootstrap end*******************************/

        // For asynchronous connect the call needs to be done after all senders and receivers are
        // created and all receiver listeners are subscribed.
        if ( !mConnectivityModule->connect() )
        {
            return false;
        }

        if ( ( mRemoteProfiler != nullptr ) && ( !mRemoteProfiler->start() ) )
        {
            FWE_LOG_WARN( "Failed to start the Remote Profiler - No remote profiling available until FWE restart" );
        }

        if ( !mCheckinSender->start() )
        {
            FWE_LOG_ERROR( "Failed to start the Checkin thread" );
            return false;
        }

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
        mIWaveGpsSource = std::make_shared<IWaveGpsSource>( signalBufferDistributor );
        std::string pathToNmeaSource;
        CANChannelNumericID canChannel{};
        CANRawFrameID canRawFrameId{};
        uint16_t latitudeStartBit{};
        uint16_t longitudeStartBit{};
        if ( config["staticConfig"].isMember( CONFIG_SECTION_IWAVE_GPS ) )
        {
            FWE_LOG_TRACE( "Found '" + CONFIG_SECTION_IWAVE_GPS + "' section in config file" );
            const auto iwaveConfig = config["staticConfig"][CONFIG_SECTION_IWAVE_GPS];
            pathToNmeaSource = iwaveConfig[IWaveGpsSource::PATH_TO_NMEA].asStringRequired();
            canChannel = mCANIDTranslator.getChannelNumericID(
                iwaveConfig[IWaveGpsSource::CAN_CHANNEL_NUMBER].asStringRequired() );
            canRawFrameId = iwaveConfig[IWaveGpsSource::CAN_RAW_FRAME_ID].asU32FromStringRequired();
            latitudeStartBit =
                static_cast<uint16_t>( iwaveConfig[IWaveGpsSource::LATITUDE_START_BIT].asU32FromStringRequired() );
            longitudeStartBit =
                static_cast<uint16_t>( iwaveConfig[IWaveGpsSource::LONGITUDE_START_BIT].asU32FromStringRequired() );
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
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                std::bind( &IWaveGpsSource::onChangeOfActiveDictionary,
                           mIWaveGpsSource.get(),
                           std::placeholders::_1,
                           std::placeholders::_2 ) );
            mIWaveGpsSource->start();
        }
        /********************************IWave GPS Example NMEA reader end******************************/
#endif

#ifdef FWE_FEATURE_EXTERNAL_GPS
        /********************************External GPS Example NMEA reader *********************************/
        mExternalGpsSource = std::make_shared<ExternalGpsSource>( signalBufferDistributor );
        bool externalGpsInitSuccessful = false;
        if ( config["staticConfig"].isMember( CONFIG_SECTION_EXTERNAL_GPS ) )
        {
            FWE_LOG_TRACE( "Found '" + CONFIG_SECTION_EXTERNAL_GPS + "' section in config file" );
            auto externalGpsConfig = config["staticConfig"][CONFIG_SECTION_EXTERNAL_GPS];
            externalGpsInitSuccessful = mExternalGpsSource->init(
                mCANIDTranslator.getChannelNumericID(
                    externalGpsConfig[ExternalGpsSource::CAN_CHANNEL_NUMBER].asStringRequired() ),
                externalGpsConfig[ExternalGpsSource::CAN_RAW_FRAME_ID].asU32FromStringRequired(),
                static_cast<uint16_t>(
                    externalGpsConfig[ExternalGpsSource::LATITUDE_START_BIT].asU32FromStringRequired() ),
                static_cast<uint16_t>(
                    externalGpsConfig[ExternalGpsSource::LONGITUDE_START_BIT].asU32FromStringRequired() ) );
        }
        else
        {
            // If not config available default to this values
            externalGpsInitSuccessful =
                mExternalGpsSource->init( mCANIDTranslator.getChannelNumericID( "EXTERNAL-GPS-CAN" ), 1, 32, 0 );
        }
        if ( externalGpsInitSuccessful )
        {
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                std::bind( &ExternalGpsSource::onChangeOfActiveDictionary,
                           mExternalGpsSource.get(),
                           std::placeholders::_1,
                           std::placeholders::_2 ) );
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
        mAaosVhalSource = std::make_shared<AaosVhalSource>( signalBufferDistributor );
        bool aaosVhalInitSuccessful = false;
        if ( config["staticConfig"].isMember( CONFIG_SECTION_AAOS_VHAL ) )
        {
            FWE_LOG_TRACE( "Found '" + CONFIG_SECTION_AAOS_VHAL + "' section in config file" );
            auto aaosConfig = config["staticConfig"][CONFIG_SECTION_AAOS_VHAL];
            aaosVhalInitSuccessful =
                mAaosVhalSource->init( mCANIDTranslator.getChannelNumericID(
                                           aaosConfig[AaosVhalSource::CAN_CHANNEL_NUMBER].asStringRequired() ),
                                       aaosConfig[AaosVhalSource::CAN_RAW_FRAME_ID].asU32FromStringRequired() );
        }
        else
        {
            // If not config available default to this values
            aaosVhalInitSuccessful =
                mAaosVhalSource->init( mCANIDTranslator.getChannelNumericID( "AAOS-VHAL-CAN" ), 1 );
        }
        if ( aaosVhalInitSuccessful )
        {
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                std::bind( &AaosVhalSource::onChangeOfActiveDictionary,
                           mAaosVhalSource.get(),
                           std::placeholders::_1,
                           std::placeholders::_2 ) );
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
            config["staticConfig"]["internalParameters"]["metricsCyclicPrintIntervalMs"].asU32Optional().get_value_or(
                0 );
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

    if ( !mCheckinSender->stop() )
    {
        FWE_LOG_ERROR( "Failed to stop the Checkin thread" );
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

    if ( !mConnectivityModule->disconnect() )
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
IoTFleetWiseEngine::ingestExternalCANMessage( const InterfaceID &interfaceId,
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
IoTFleetWiseEngine::setVehicleProperty( uint32_t signalId, const DecodedSignalValue &value )
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
    if ( mConnectivityModule == nullptr || mCollectionSchemeManagerPtr == nullptr || mSenderVehicleData == nullptr ||
         mOBDOverCANModule == nullptr )
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

    status += "Payloads sent: " + std::to_string( mSenderVehicleData->getPayloadCountSent() ) + "\n\n";
    return status;
}

} // namespace IoTFleetWise
} // namespace Aws
