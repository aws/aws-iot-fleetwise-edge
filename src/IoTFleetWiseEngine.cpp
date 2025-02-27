// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/IoTFleetWiseEngine.h"
#include "aws/iotfleetwise/AwsBootstrap.h"
#include "aws/iotfleetwise/AwsIotConnectivityModule.h"
#include "aws/iotfleetwise/AwsSDKMemoryManager.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ConsoleLogger.h"
#include "aws/iotfleetwise/DataSenderManager.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ILogger.h"
#include "aws/iotfleetwise/IoTFleetWiseVersion.h"
#include "aws/iotfleetwise/LogLevel.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/MqttClientWrapper.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <algorithm>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/iot/Mqtt5Client.h>
#include <boost/optional/optional.hpp>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#ifdef FWE_FEATURE_GREENGRASSV2
#include "aws/iotfleetwise/AwsGreengrassV2ConnectivityModule.h"
#ifdef FWE_FEATURE_S3
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#endif
#endif
#ifdef FWE_FEATURE_S3
#include "aws/iotfleetwise/Credentials.h"
#include "aws/iotfleetwise/TransferManagerWrapper.h" // IWYU pragma: keep
#include <aws/core/client/ClientConfiguration.h>     // IWYU pragma: keep
#include <aws/s3/S3Client.h>                         // IWYU pragma: keep
#include <aws/s3/S3ServiceClientModel.h>             // IWYU pragma: keep
#include <aws/transfer/TransferManager.h>            // IWYU pragma: keep
#endif
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "aws/iotfleetwise/DataSenderIonWriter.h"
#include "aws/iotfleetwise/VisionSystemDataSender.h"
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
#include "aws/iotfleetwise/CommandResponseDataSender.h"
#include "aws/iotfleetwise/SignalTypes.h"
#endif
#ifdef FWE_FEATURE_SOMEIP
#include "aws/iotfleetwise/ExampleSomeipInterfaceWrapper.h"
#include "v1/commonapi/DeviceShadowOverSomeipInterface.hpp"
#include "v1/commonapi/ExampleSomeipInterfaceProxy.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <vsomeip/vsomeip.hpp>
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "aws/iotfleetwise/LastKnownStateDataSender.h"
#include "aws/iotfleetwise/LastKnownStateInspector.h"
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#endif
#ifdef FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES
#include "aws/iotfleetwise/CustomFunctionMath.h"
#endif
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
#include <stdexcept>
#endif
#ifdef FWE_FEATURE_SCRIPT_ENGINE
#include <aws/s3/model/GetObjectRequest.h>
#endif

extern "C"
{
    // coverity[cert_msc54_cpp_violation] False positive - function does have C linkage
    static void
    signalHandler( int signum )
    {
        // Very few things are safe in a signal handler. So we never do anything other than set the atomic int, not even
        // print a message: https://stackoverflow.com/a/16891799
        Aws::IoTFleetWise::gSignal = signum;
    }

    // coverity[cert_msc54_cpp_violation] False positive - function does have C linkage
    static void
    segFaultHandler( int signum )
    {
        static_cast<void>( signum );
        // SIGSEGV handlers should never return. We have to abort:
        // https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
        // coverity[autosar_cpp14_m18_0_3_violation]
        // coverity[misra_cpp_2008_rule_18_0_3_violation]
        abort();
    }

    // coverity[cert_msc54_cpp_violation] False positive - function does have C linkage
    static void
    abortHandler( int signum )
    {
        static_cast<void>( signum );
        // Very few things are safe in a signal handler. Flushing streams isn't normally safe,
        // unless we can guarantee that nothing is currently using the stream:
        // https://www.gnu.org/software/libc/manual/html_node/Nonreentrancy.html
        // So we use an atomic int (signal handler safe) to check whether the program stopped while
        // in the middle of a log call. Assuming that we are not using the log stream (stdout)
        // directly anywhere else, flushing should be safe here.
        if ( Aws::IoTFleetWise::gOngoingLogMessage == 0 )
        {
            Aws::IoTFleetWise::LoggingModule::flush();
        }
    }
}

namespace Aws
{
namespace IoTFleetWise
{

// coverity[autosar_cpp14_a2_11_1_violation] volatile required as it will be modified by a signal handler
volatile sig_atomic_t gSignal = 0; // NOLINT Global signal
static constexpr uint64_t DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS = 10000;
static constexpr uint64_t DEFAULT_FETCH_QUEUE_SIZE = 1000;
static const std::string CAN_INTERFACE_TYPE = "canInterface";
static const std::string EXTERNAL_CAN_INTERFACE_TYPE = "externalCanInterface";
static const std::string OBD_INTERFACE_TYPE = "obdInterface";
static const std::string NAMED_SIGNAL_INTERFACE_TYPE = "namedSignalInterface";
#ifdef FWE_FEATURE_ROS2
static const std::string ROS2_INTERFACE_TYPE = "ros2Interface";
#endif
#ifdef FWE_FEATURE_SOMEIP
static const std::string SOMEIP_TO_CAN_BRIDGE_INTERFACE_TYPE = "someipToCanBridgeInterface";
static const std::string SOMEIP_COLLECTION_INTERFACE_TYPE = "someipCollectionInterface";
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
#ifdef FWE_FEATURE_SOMEIP
static const std::string SOMEIP_COMMAND_INTERFACE_TYPE = "someipCommandInterface";
#endif
static const std::string CAN_COMMAND_INTERFACE_TYPE = "canCommandInterface";
static const std::unordered_map<std::string, CanCommandDispatcher::CommandConfig>
    EXAMPLE_CAN_INTERFACE_SUPPORTED_ACTUATOR_MAP = {
        { "Vehicle.actuator6", { 0x00000123, 0x00000456, SignalType::INT32 } },
        { "Vehicle.actuator7", { 0x80000789, 0x80000ABC, SignalType::DOUBLE } },
};
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
static const std::string IWAVE_GPS_INTERFACE_TYPE = "iWaveGpsInterface";
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
static const std::string EXTERNAL_GPS_INTERFACE_TYPE = "externalGpsInterface";
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
static const std::string AAOS_VHAL_INTERFACE_TYPE = "aaosVhalInterface";
#endif
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
static const std::string UDS_DTC_INTERFACE = "exampleUDSInterface";
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

void
IoTFleetWiseEngine::configureSignalHandlers()
{
    signal( SIGINT, signalHandler );
    signal( SIGTERM, signalHandler );
    signal( SIGUSR1, signalHandler );
    signal( SIGSEGV, segFaultHandler );
    // Mainly to handle when a thread is terminated due to uncaught exception
    signal( SIGABRT, abortHandler );
    // Ignore SIGPIPE, as per
    // https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/basic-use.html#sdk-setting-options
    // coverity[misra_cpp_2008_rule_5_2_9_violation] Using SIG_IGN is the standard method to ignore signals
    // coverity[autosar_cpp14_m5_2_9_violation] Using SIG_IGN is the standard method to ignore signals
    signal( SIGPIPE, SIG_IGN ); // NOLINT
}

std::string
IoTFleetWiseEngine::getVersion()
{
    return "FWE Version: " + std::string( &FWE_VERSION_PROJECT_VERSION[0] ) +
           ", git tag: " + std::string( &FWE_VERSION_GIT_TAG[0] ) +
           ", git commit sha: " + std::string( &FWE_VERSION_GIT_COMMIT_SHA[0] ) +
           ", Build time: " + std::string( &FWE_VERSION_BUILD_TIME[0] );
}

void
IoTFleetWiseEngine::configureLogging( const Json::Value &config )
{
    auto logLevel = LogLevel::Trace;
    stringToLogLevel( config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString(), logLevel );
    gSystemWideLogLevel = logLevel;

    auto logColorOption = LogColorOption::Auto;
    if ( config["staticConfig"]["internalParameters"].isMember( "logColor" ) )
    {
        std::string logColorConfig = config["staticConfig"]["internalParameters"]["logColor"].asString();
        if ( !stringToLogColorOption( logColorConfig, logColorOption ) )
        {
            FWE_LOG_ERROR( "Invalid logColor config: " + logColorConfig );
        }
    }
    gLogColorOption = logColorOption;
}

int
IoTFleetWiseEngine::signalToExitCode( int signalNumber )
{
    switch ( signalNumber )
    {
    case SIGUSR1:
        FWE_LOG_ERROR( "Fatal error, stopping" );
        return -1;
    case SIGINT:
    case SIGTERM:
        FWE_LOG_INFO( "Stopping" );
        return 0;
    default:
        FWE_LOG_WARN( "Received unexpected signal " + std::to_string( signalNumber ) );
        return 0;
    }
}

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

#ifdef FWE_FEATURE_SOMEIP
static std::shared_ptr<ExampleSomeipInterfaceWrapper>
createExampleSomeipInterfaceWrapper( const std::string &applicationName,
                                     const std::string &exampleInstance,
                                     RawData::BufferManager *rawDataBufferManager,
                                     bool subscribeToLongRunningCommandStatus )
{
    return std::make_shared<ExampleSomeipInterfaceWrapper>(
        "local",
        exampleInstance,
        applicationName,
        []( std::string domain,
            std::string instance,
            std::string connection ) -> std::shared_ptr<v1::commonapi::ExampleSomeipInterfaceProxy<>> {
            return CommonAPI::Runtime::get()->buildProxy<v1::commonapi::ExampleSomeipInterfaceProxy>(
                domain, instance, connection );
        },
        rawDataBufferManager,
        subscribeToLongRunningCommandStatus );
}
#endif

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
            mCacheAndPersist = std::make_shared<CacheAndPersist>(
                getAbsolutePath( persistencyPath, configFileDirectoryPath ).string(),
                config["staticConfig"]["persistency"]["persistencyPartitionMaxSize"].asSizeRequired() );
            if ( !mCacheAndPersist->init() )
            {
                FWE_LOG_ERROR( "Failed to init persistency library" );
            }
            persistencyUploadRetryIntervalMs =
                config["staticConfig"]["persistency"]["persistencyUploadRetryIntervalMs"].asU64Optional().get_value_or(
                    DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS );
            // Payload Manager for offline data management
            mPayloadManager = std::make_shared<PayloadManager>( mCacheAndPersist );
        }
        else
        {
            FWE_LOG_INFO( "Persistency feature is disabled in the configuration." );
#ifdef FWE_FEATURE_STORE_AND_FORWARD
            FWE_LOG_INFO( "Disabling Store and Forward feature as persistency is disabled." );
            mStoreAndForwardEnabled = false;
#endif
        }
        /*************************Payload Manager and Persistency library bootstrap end************/

        /*************************CAN InterfaceID to InternalID Translator begin*********/
        for ( unsigned i = 0; i < config["networkInterfaces"].getArraySizeRequired(); i++ )
        {
            auto networkInterface = config["networkInterfaces"][i];
            auto networkInterfaceType = networkInterface["type"].asStringRequired();
            if ( ( networkInterfaceType == CAN_INTERFACE_TYPE ) ||
                 ( networkInterfaceType == EXTERNAL_CAN_INTERFACE_TYPE )
#ifdef FWE_FEATURE_SOMEIP
                 || ( networkInterfaceType == SOMEIP_TO_CAN_BRIDGE_INTERFACE_TYPE )
#endif
            )
            {
                mCANIDTranslator.add( networkInterface["interfaceId"].asStringRequired() );
            }
        }
        /*************************CAN InterfaceID to InternalID Translator end*********/

        /**************************Connectivity bootstrap begin*******************************/
        // Pass on the AWS SDK Bootstrap handle to the IoTModule.
        auto awsSdkLogLevel = AwsBootstrap::logLevelFromString(
            config["staticConfig"]["internalParameters"]["awsSdkLogLevel"].asStringOptional().get_value_or( "Warn" ) );
        auto bootstrapPtr = AwsBootstrap::getInstance( awsSdkLogLevel ).getClientBootStrap();
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
        TopicConfigArgs topicConfigArgs;
        topicConfigArgs.iotFleetWisePrefix = mqttConfig["iotFleetWiseTopicPrefix"].asStringOptional();
        topicConfigArgs.commandsPrefix = mqttConfig["commandsTopicPrefix"].asStringOptional();
        topicConfigArgs.deviceShadowPrefix = mqttConfig["deviceShadowTopicPrefix"].asStringOptional();
        topicConfigArgs.jobsPrefix = mqttConfig["jobsTopicPrefix"].asStringOptional();
        topicConfigArgs.metricsTopic = mqttConfig["metricsUploadTopic"].asStringOptional().get_value_or( "" );
        topicConfigArgs.logsTopic = mqttConfig["loggingUploadTopic"].asStringOptional().get_value_or( "" );
        mTopicConfig = std::make_unique<TopicConfig>( clientId, topicConfigArgs );

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
                mBuilderWrapper = std::make_unique<MqttClientBuilderWrapper>( std::move( builder ) );
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
                rootCA, clientId, *mBuilderWrapper, *mTopicConfig, mqttConnectionConfig );

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
            mConnectivityModule = std::make_shared<AwsGreengrassV2ConnectivityModule>( bootstrapPtr, *mTopicConfig );
#ifdef FWE_FEATURE_S3
            mAwsCredentialsProvider = std::make_shared<Aws::Auth::DefaultAWSCredentialsProviderChain>();
#endif
        }
#endif
        else if ( ( mConnectivityModuleConfigHook != nullptr ) && mConnectivityModuleConfigHook( config ) )
        {
            // External connectivity module was configured
        }
        else
        {
            FWE_LOG_ERROR( "Unknown connection type: " + connectionType );
            return false;
        }

        mReceiverCollectionSchemeList = mConnectivityModule->createReceiver( mTopicConfig->collectionSchemesTopic );
        mReceiverDecoderManifest = mConnectivityModule->createReceiver( mTopicConfig->decoderManifestTopic );
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        if ( mStoreAndForwardEnabled )
        {
            // Receivers to receive Store and Forward Data Upload Requests
            mReceiverIotJob = mConnectivityModule->createReceiver( mTopicConfig->jobNotificationTopic );
            mReceiverJobDocumentAccepted =
                mConnectivityModule->createReceiver( mTopicConfig->getJobExecutionAcceptedTopic );
            mReceiverJobDocumentRejected =
                mConnectivityModule->createReceiver( mTopicConfig->getJobExecutionRejectedTopic );
            mReceiverPendingJobsAccepted =
                mConnectivityModule->createReceiver( mTopicConfig->getPendingJobExecutionsAcceptedTopic );
            mReceiverPendingJobsRejected =
                mConnectivityModule->createReceiver( mTopicConfig->getPendingJobExecutionsRejectedTopic );
            mReceiverUpdateIotJobStatusAccepted =
                mConnectivityModule->createReceiver( mTopicConfig->updateJobExecutionAcceptedTopic );
            mReceiverUpdateIotJobStatusRejected =
                mConnectivityModule->createReceiver( mTopicConfig->updateJobExecutionRejectedTopic );
            mReceiverCanceledIoTJobs =
                mConnectivityModule->createReceiver( mTopicConfig->jobCancellationInProgressTopic );
        }
#endif

        mMqttSender = mConnectivityModule->createSender();

#ifdef FWE_FEATURE_REMOTE_COMMANDS
        std::shared_ptr<IReceiver> receiverCommandRequest;
        std::shared_ptr<IReceiver> receiverRejectedCommandResponse;
        std::shared_ptr<IReceiver> receiverAcceptedCommandResponse;
        receiverCommandRequest = mConnectivityModule->createReceiver( mTopicConfig->commandRequestTopic );
        // The accepted/rejected messages are always sent regardless of whether we are subscribing to the topics or
        // not. So even if we don't need to receive them, we subscribe to them just to ensure we don't log any
        // error.
        receiverAcceptedCommandResponse =
            mConnectivityModule->createReceiver( mTopicConfig->commandResponseAcceptedTopic );
        receiverRejectedCommandResponse =
            mConnectivityModule->createReceiver( mTopicConfig->commandResponseRejectedTopic );
#endif

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        std::shared_ptr<IReceiver> receiverLastKnownStateConfig =
            mConnectivityModule->createReceiver( mTopicConfig->lastKnownStateConfigTopic );
#endif

#ifdef FWE_FEATURE_SOMEIP
        if ( !config["staticConfig"].isMember( "deviceShadowOverSomeip" ) )
        {
            FWE_LOG_TRACE( "DeviceShadowOverSomeip is disabled as no deviceShadowOverSomeip member in staticConfig" );
        }
        else
        {
            std::shared_ptr<IReceiver> receiverDeviceShadow =
                mConnectivityModule->createReceiver( mTopicConfig->deviceShadowPrefix + "#" );
            mDeviceShadowOverSomeip = std::make_shared<DeviceShadowOverSomeip>( *mMqttSender );
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            receiverDeviceShadow->subscribeToDataReceived( std::bind(
                &DeviceShadowOverSomeip::onDataReceived, mDeviceShadowOverSomeip.get(), std::placeholders::_1 ) );
            mDeviceShadowOverSomeipInstanceName =
                config["staticConfig"]["deviceShadowOverSomeip"]["someipInstance"].asStringOptional().get_value_or(
                    "commonapi.DeviceShadowOverSomeipInterface" );
            if ( !CommonAPI::Runtime::get()->registerService(
                     "local",
                     mDeviceShadowOverSomeipInstanceName,
                     mDeviceShadowOverSomeip,
                     config["staticConfig"]["deviceShadowOverSomeip"]["someipApplicationName"].asStringRequired() ) )
            {
                FWE_LOG_ERROR( "Failed to register DeviceShadowOverSomeip service" );
                return false;
            }
        }
#endif

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
            mRawDataBufferManager = std::make_unique<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
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
                *mMqttSender,
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

        mSignalBufferDistributor.registerQueue( signalBuffer );

        // Create the Data Inspection Queue
        mCollectedDataReadyToPublish = std::make_shared<DataSenderQueue>(
            config["staticConfig"]["internalParameters"]["readyToPublishDataBufferSize"].asSizeRequired(),
            "Collected Data",
            TraceAtomicVariable::QUEUE_INSPECTION_TO_SENDER );

#ifdef FWE_FEATURE_STORE_AND_FORWARD
        if ( mStoreAndForwardEnabled )
        {
            const auto persistencyPath = config["staticConfig"]["persistency"]["persistencyPath"].asStringRequired();

            mStreamManager = std::make_unique<Aws::IoTFleetWise::Store::StreamManager>(
                persistencyPath,
                std::make_unique<DataSenderProtoWriter>( mCANIDTranslator, mRawDataBufferManager.get() ),
                config["staticConfig"]["publishToCloudParameters"]["maxPublishMessageCount"].asU32Required() );
        }
#endif

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
        auto telemetryDataSender = std::make_unique<TelemetryDataSender>(
            *mMqttSender,
            std::make_unique<DataSenderProtoWriter>( mCANIDTranslator, mRawDataBufferManager.get() ),
            payloadAdaptionConfigUncompressed,
            payloadAdaptionConfigCompressed
#ifdef FWE_FEATURE_STORE_AND_FORWARD
            ,
            mStreamManager.get()
#endif
        );

#ifdef FWE_FEATURE_STORE_AND_FORWARD
        if ( mStoreAndForwardEnabled )
        {
            mRateLimiter = std::make_unique<RateLimiter>(
                config["staticConfig"]["storeAndForward"]["forwardMaxTokens"].asU32Optional().get_value_or(
                    DEFAULT_MAX_TOKENS ),
                config["staticConfig"]["storeAndForward"]["forwardTokenRefillsPerSecond"].asU32Optional().get_value_or(
                    DEFAULT_TOKEN_REFILLS_PER_SECOND ) );
            mStreamForwarder = std::make_unique<Aws::IoTFleetWise::Store::StreamForwarder>(
                *mStreamManager, *telemetryDataSender, *mRateLimiter );

            // Start the forwarder
            if ( !mStreamForwarder->start() )
            {
                FWE_LOG_ERROR( "Failed to init and start the Stream Forwarder" );
                return false;
            }
        }
#endif

        mDataSenders.emplace( SenderDataType::TELEMETRY, std::move( telemetryDataSender ) );

        mFetchQueue = std::make_shared<FetchRequestQueue>(
            config["staticConfig"]["internalParameters"]["maxFetchQueueSize"].asU32Optional().get_value_or(
                DEFAULT_FETCH_QUEUE_SIZE ),
            "Data Fetch Queue" );
        auto minFetchTriggerIntervalMs =
            config["staticConfig"]["internalParameters"]["minFetchTriggerIntervalMs"].asU32Optional().get_value_or(
                MIN_FETCH_TRIGGER_MS );
        mCollectionInspectionEngine = std::make_shared<CollectionInspectionEngine>( mRawDataBufferManager.get(),
                                                                                    minFetchTriggerIntervalMs,
                                                                                    mFetchQueue,
                                                                                    true
#ifdef FWE_FEATURE_STORE_AND_FORWARD
                                                                                    ,
                                                                                    mStreamForwarder.get()
#endif
        );
        mCollectionInspectionWorkerThread = std::make_shared<CollectionInspectionWorkerThread>(
            *mCollectionInspectionEngine,
            signalBuffer,
            mCollectedDataReadyToPublish,
            config["staticConfig"]["threadIdleTimes"]["inspectionThreadIdleTimeMs"].asU32Required(),
            mRawDataBufferManager.get() );
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        signalBuffer->subscribeToNewDataAvailable( std::bind( &CollectionInspectionWorkerThread::onNewDataAvailable,
                                                              mCollectionInspectionWorkerThread.get() ) );
        /*************************Inspection Engine bootstrap end***********************************/

        /*************************Store and Forward IoT Jobs bootstrap begin************************/
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        if ( mStoreAndForwardEnabled )
        {
            mIoTJobsDataRequestHandler =
                std::make_unique<IoTJobsDataRequestHandler>( *mMqttSender,
                                                             *mReceiverIotJob,
                                                             *mReceiverJobDocumentAccepted,
                                                             *mReceiverJobDocumentRejected,
                                                             *mReceiverPendingJobsAccepted,
                                                             *mReceiverPendingJobsRejected,
                                                             *mReceiverUpdateIotJobStatusAccepted,
                                                             *mReceiverUpdateIotJobStatusRejected,
                                                             *mReceiverCanceledIoTJobs,
                                                             *mStreamManager,
                                                             *mStreamForwarder,
                                                             clientId );

            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mConnectivityModule->subscribeToConnectionEstablished(
                std::bind( &IoTJobsDataRequestHandler::onConnectionEstablished, mIoTJobsDataRequestHandler.get() ) );
        }
#endif
        /*************************Store and Forward IoT Jobs bootstrap end**************************/

        /*************************DataSender bootstrap begin*********************************/
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        DataSenderIonWriter *ionWriter = nullptr;
        VisionSystemDataSender *visionSystemDataSender = nullptr;
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
            mS3Sender = std::make_unique<S3Sender>(
                createTransferManagerWrapper, config["staticConfig"]["s3Upload"]["multipartSize"].asSizeRequired() );
            auto ionWriterPtr = std::make_unique<DataSenderIonWriter>( mRawDataBufferManager.get(), clientId );
            ionWriter = ionWriterPtr.get();
            auto visionSystemDataSenderPtr = std::make_unique<VisionSystemDataSender>(
                *mCollectedDataReadyToPublish, *mS3Sender, std::move( ionWriterPtr ), clientId );
            visionSystemDataSender = visionSystemDataSenderPtr.get();
            mDataSenders.emplace( SenderDataType::VISION_SYSTEM, std::move( visionSystemDataSenderPtr ) );
        }
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
        mCommandResponses = std::make_shared<DataSenderQueue>(
            config["staticConfig"]["internalParameters"]["readyToPublishCommandResponsesBufferSize"]
                .asSizeOptional()
                .get_value_or( 100 ),
            "Command Responses",
            TraceAtomicVariable::QUEUE_PENDING_COMMAND_RESPONSES );
        size_t maxConcurrentCommandRequests =
            config["staticConfig"]["internalParameters"]["maxConcurrentCommandRequests"].asSizeOptional().get_value_or(
                100 );
        mActuatorCommandManager = std::make_shared<ActuatorCommandManager>(
            mCommandResponses, maxConcurrentCommandRequests, mRawDataBufferManager.get() );

        mDataSenders.emplace( SenderDataType::COMMAND_RESPONSE,
                              std::make_unique<CommandResponseDataSender>( *mMqttSender ) );
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        mLastKnownStateDataReadyToPublish = std::make_shared<DataSenderQueue>(
            config["staticConfig"]["internalParameters"]["readyToPublishDataBufferSize"].asSizeRequired(),
            "LastKnownState data",
            TraceAtomicVariable::QUEUE_LAST_KNOWN_STATE_INSPECTION_TO_SENDER );
        mDataSenders.emplace(
            SenderDataType::LAST_KNOWN_STATE,
            std::make_unique<LastKnownStateDataSender>(
                *mMqttSender,
                config["staticConfig"]["publishToCloudParameters"]["maxPublishLastKnownStateMessageCount"]
                    .asU32Optional()
                    .get_value_or( 1000 ) ) );
#endif

        std::vector<std::shared_ptr<DataSenderQueue>> dataToSendQueues = {
#ifdef FWE_FEATURE_REMOTE_COMMANDS
            mCommandResponses,
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
            mLastKnownStateDataReadyToPublish,
#endif
            mCollectedDataReadyToPublish };
        mDataSenderManagerWorkerThread = std::make_shared<DataSenderManagerWorkerThread>(
            *mConnectivityModule,
            std::make_unique<DataSenderManager>( mDataSenders, mPayloadManager.get() ),
            persistencyUploadRetryIntervalMs,
            dataToSendQueues );
        if ( !mDataSenderManagerWorkerThread->start() )
        {
            FWE_LOG_ERROR( "Failed to init and start the Data Sender" );
            return false;
        }

        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mCollectedDataReadyToPublish->subscribeToNewDataAvailable(
            std::bind( &DataSenderManagerWorkerThread::onDataReadyToPublish, mDataSenderManagerWorkerThread.get() ) );

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mLastKnownStateDataReadyToPublish->subscribeToNewDataAvailable(
            std::bind( &DataSenderManagerWorkerThread::onDataReadyToPublish, mDataSenderManagerWorkerThread.get() ) );
#endif
        /*************************DataSender bootstrap end*********************************/

        /*************************CollectionScheme Ingestion bootstrap begin*********************************/

        // CollectionScheme Ingestion module executes in the context for the offboardconnectivity thread. Upcoming
        // messages are expected to come either on the decoder manifest topic or the collectionScheme topic or both
        // ( eventually ).
        mSchemaPtr =
            std::make_shared<Schema>( *mReceiverDecoderManifest, *mReceiverCollectionSchemeList, *mMqttSender );
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        if ( receiverLastKnownStateConfig != nullptr )
        {
            mLastKnownStateSchema = std::make_unique<LastKnownStateSchema>( *receiverLastKnownStateConfig );
        }
#endif
        /*****************************CollectionScheme Management bootstrap begin*****************************/

        // Allow CollectionSchemeManagement to send checkins through the Schema Object Callback
        mCheckinSender = std::make_shared<CheckinSender>(
            mSchemaPtr,
            config["staticConfig"]["publishToCloudParameters"]["collectionSchemeManagementCheckinIntervalMs"]
                .asU32Required() );

        // Create and connect the CollectionScheme Manager
        mCollectionSchemeManagerPtr = std::make_shared<CollectionSchemeManager>(
            mCacheAndPersist,
            mCANIDTranslator,
            mCheckinSender,
            mRawDataBufferManager.get()
#ifdef FWE_FEATURE_REMOTE_COMMANDS
                ,
            [this]() -> std::unordered_map<InterfaceID, std::vector<std::string>> {
                return mActuatorCommandManager->getActuatorNames();
            }
#endif
            ,
            config["staticConfig"]["threadIdleTimes"]["collectionSchemeManagerThreadIdleTimeMs"]
                .asU32Optional()
                .get_value_or( 0 ) );

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
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        if ( mLastKnownStateSchema != nullptr )
        {
            mLastKnownStateSchema->subscribeToLastKnownStateReceived(
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                std::bind( &CollectionSchemeManager::onStateTemplatesChanged,
                           mCollectionSchemeManagerPtr.get(),
                           std::placeholders::_1 ) );
        }
#endif

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
                           visionSystemDataSender,
                           std::placeholders::_1 ) );
        }

        if ( ionWriter != nullptr )
        {
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                std::bind( &DataSenderIonWriter::onChangeOfActiveDictionary,
                           ionWriter,
                           std::placeholders::_1,
                           std::placeholders::_2 ) );
        }
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        if ( mStoreAndForwardEnabled )
        {
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToCollectionSchemeListChange(
                std::bind( &Aws::IoTFleetWise::Store::StreamManager::onChangeCollectionSchemeList,
                           mStreamManager.get(),
                           std::placeholders::_1 ) );
        }
#endif

        /*************************DataFetchManager bootstrap begin*********************************/

        mDataFetchManager = std::make_shared<DataFetchManager>( mFetchQueue );
        mFetchQueue->subscribeToNewDataAvailable(
            std::bind( &DataFetchManager::onNewFetchRequestAvailable, mDataFetchManager.get() ) );
        // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
        mCollectionSchemeManagerPtr->subscribeToFetchMatrixChange(
            std::bind( &DataFetchManager::onChangeFetchMatrix, mDataFetchManager.get(), std::placeholders::_1 ) );
        /********************************Data source bootstrap start*******************************/

        auto obdOverCANModuleInit = false;
        mCANDataConsumer = std::make_unique<CANDataConsumer>( mSignalBufferDistributor );
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
                if ( !canSourcePtr->connect() )
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
                    auto obdConfig = networkInterfaceConfig[OBD_INTERFACE_TYPE];
                    mOBDOverCANModule = std::make_shared<OBDOverCANModule>(
                        mSignalBufferDistributor,
                        obdConfig["interfaceName"].asStringRequired(),
                        obdConfig["pidRequestIntervalSeconds"].asU32Required(),
                        obdConfig["dtcRequestIntervalSeconds"].asU32Required(),
                        // Broadcast mode is enabled by default if not defined in config:
                        obdConfig["broadcastRequests"].asBoolOptional().get_value_or( true ) );

                    obdOverCANModuleInit = true;
                    // Connect the OBD Module
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
                    mCollectionSchemeManagerPtr->subscribeToInspectionMatrixChange( std::bind(
                        &OBDOverCANModule::onChangeInspectionMatrix, mOBDOverCANModule.get(), std::placeholders::_1 ) );
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
                mExternalCANDataSource = std::make_unique<ExternalCANDataSource>( mCANIDTranslator, *mCANDataConsumer );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &ExternalCANDataSource::onChangeOfActiveDictionary,
                               mExternalCANDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
            }
            else if ( interfaceType == NAMED_SIGNAL_INTERFACE_TYPE )
            {
                if ( mNamedSignalDataSource != nullptr )
                {
                    continue;
                }
                mNamedSignalDataSource =
                    std::make_shared<NamedSignalDataSource>( interfaceId, mSignalBufferDistributor );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &NamedSignalDataSource::onChangeOfActiveDictionary,
                               mNamedSignalDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
            }
#ifdef FWE_FEATURE_SOMEIP
            else if ( interfaceType == SOMEIP_COLLECTION_INTERFACE_TYPE )
            {
                if ( mSomeipDataSource != nullptr )
                {
                    continue;
                }
                // coverity[autosar_cpp14_a20_8_4_violation] Shared pointer interface required for unit testing
                auto namedSignalDataSource =
                    std::make_shared<NamedSignalDataSource>( interfaceId, mSignalBufferDistributor );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &NamedSignalDataSource::onChangeOfActiveDictionary,
                               namedSignalDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
                auto someipCollectionInterfaceConfig = networkInterfaceConfig[SOMEIP_COLLECTION_INTERFACE_TYPE];
                mSomeipDataSource = std::make_unique<SomeipDataSource>(
                    createExampleSomeipInterfaceWrapper(
                        someipCollectionInterfaceConfig["someipApplicationName"].asStringRequired(),
                        someipCollectionInterfaceConfig["someipInstance"].asStringOptional().get_value_or(
                            "commonapi.ExampleSomeipInterface" ),
                        mRawDataBufferManager.get(),
                        false ),
                    std::move( namedSignalDataSource ),
                    mRawDataBufferManager.get(),
                    someipCollectionInterfaceConfig["cyclicUpdatePeriodMs"].asU32Required() );
                if ( !mSomeipDataSource->connect() )
                {
                    FWE_LOG_ERROR( "Failed to initialize SOME/IP data source" );
                    return false;
                }
            }
#endif
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
            else if ( interfaceType == UDS_DTC_INTERFACE )
            {
                FWE_LOG_INFO( "UDS Template DTC Interface Type received" );
                mDiagnosticNamedSignalDataSource =
                    std::make_shared<NamedSignalDataSource>( interfaceId, mSignalBufferDistributor );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &NamedSignalDataSource::onChangeOfActiveDictionary,
                               mDiagnosticNamedSignalDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
                std::vector<EcuConfig> remoteDiagnosticInterfaceConfig;
                auto ecuConfiguration = networkInterfaceConfig[UDS_DTC_INTERFACE];
                for ( unsigned j = 0; j < networkInterfaceConfig[UDS_DTC_INTERFACE]["configs"].getArraySizeRequired();
                      j++ )
                {
                    auto ecu = ecuConfiguration["configs"][j];
                    auto canInterface = ecu["can"];
                    EcuConfig ecuConfig;
                    ecuConfig.ecuName = static_cast<std::string>( ecu["name"].asStringRequired() );
                    ecuConfig.canBus = static_cast<std::string>( canInterface["interfaceName"].asStringRequired() );
                    try
                    {
                        ecuConfig.targetAddress =
                            static_cast<int>( std::stoi( ecu["targetAddress"].asStringRequired(), nullptr, 0 ) );
                        ecuConfig.physicalRequestID = static_cast<uint32_t>(
                            std::stoi( canInterface["physicalRequestID"].asStringRequired(), nullptr, 0 ) );
                        ecuConfig.physicalResponseID = static_cast<uint32_t>(
                            std::stoi( canInterface["physicalResponseID"].asStringRequired(), nullptr, 0 ) );
                        ecuConfig.functionalAddress = static_cast<uint32_t>(
                            std::stoi( canInterface["functionalAddress"].asStringRequired(), nullptr, 0 ) );
                    }
                    catch ( const std::invalid_argument &err )
                    {
                        FWE_LOG_ERROR( "Could not parse received remote diagnostics interface configuration: " +
                                       std::string( err.what() ) );
                        return false;
                    }
                    remoteDiagnosticInterfaceConfig.emplace_back( ecuConfig );
                }
                mExampleDiagnosticInterface = std::make_shared<ExampleUDSInterface>( remoteDiagnosticInterfaceConfig );
                if ( !mExampleDiagnosticInterface->start() )
                {
                    FWE_LOG_ERROR( "Failed to initialize the Template Interface" );
                    return false;
                }
            }
#endif
#ifdef FWE_FEATURE_ROS2
            else if ( interfaceType == ROS2_INTERFACE_TYPE )
            {
                ROS2DataSourceConfig ros2Config;
                if ( !ROS2DataSourceConfig::parseFromJson( networkInterfaceConfig, ros2Config ) )
                {
                    return false;
                }
                mROS2DataSource = std::make_shared<ROS2DataSource>(
                    ros2Config, mSignalBufferDistributor, mRawDataBufferManager.get() );
                mROS2DataSource->connect();
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &ROS2DataSource::onChangeOfActiveDictionary,
                               mROS2DataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
            }
#endif
#ifdef FWE_FEATURE_SOMEIP
            else if ( interfaceType == SOMEIP_TO_CAN_BRIDGE_INTERFACE_TYPE )
            {
                auto canChannelId = mCANIDTranslator.getChannelNumericID( interfaceId );
                auto someipToCanBridgeConfig = networkInterfaceConfig[SOMEIP_TO_CAN_BRIDGE_INTERFACE_TYPE];
                auto bridge = std::make_unique<SomeipToCanBridge>(
                    static_cast<uint16_t>( someipToCanBridgeConfig["someipServiceId"].asU32FromStringRequired() ),
                    static_cast<uint16_t>( someipToCanBridgeConfig["someipInstanceId"].asU32FromStringRequired() ),
                    static_cast<uint16_t>( someipToCanBridgeConfig["someipEventId"].asU32FromStringRequired() ),
                    static_cast<uint16_t>( someipToCanBridgeConfig["someipEventGroupId"].asU32FromStringRequired() ),
                    someipToCanBridgeConfig["someipApplicationName"].asStringRequired(),
                    canChannelId,
                    *mCANDataConsumer,
                    []( std::string name ) -> std::shared_ptr<vsomeip::application> {
                        return vsomeip::runtime::get()->create_application( name );
                    },
                    []( std::string name ) {
                        vsomeip::runtime::get()->remove_application( name );
                    } );
                if ( !bridge->connect() )
                {
                    FWE_LOG_ERROR( "Failed to initialize SomeipToCanBridge" );
                    return false;
                }
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &SomeipToCanBridge::onChangeOfActiveDictionary,
                               bridge.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
                mSomeipToCanBridges.push_back( std::move( bridge ) );
            }
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
#ifdef FWE_FEATURE_SOMEIP
            else if ( interfaceType == SOMEIP_COMMAND_INTERFACE_TYPE )
            {
                if ( mExampleSomeipCommandDispatcher != nullptr )
                {
                    continue;
                }
                auto exampleSomeipInterfaceWrapper = createExampleSomeipInterfaceWrapper(
                    networkInterfaceConfig[SOMEIP_COMMAND_INTERFACE_TYPE]["someipApplicationName"].asStringRequired(),
                    networkInterfaceConfig[SOMEIP_COMMAND_INTERFACE_TYPE]["someipInstance"]
                        .asStringOptional()
                        .get_value_or( "commonapi.ExampleSomeipInterface" ),
                    mRawDataBufferManager.get(),
                    true );
                mExampleSomeipCommandDispatcher =
                    std::make_shared<SomeipCommandDispatcher>( exampleSomeipInterfaceWrapper );
                if ( !mActuatorCommandManager->registerDispatcher( interfaceId, mExampleSomeipCommandDispatcher ) )
                {
                    return false;
                }
            }
#endif
            else if ( interfaceType == CAN_COMMAND_INTERFACE_TYPE )
            {
                if ( mCanCommandDispatcher != nullptr )
                {
                    continue;
                }
                mCanCommandDispatcher = std::make_shared<CanCommandDispatcher>(
                    EXAMPLE_CAN_INTERFACE_SUPPORTED_ACTUATOR_MAP,
                    networkInterfaceConfig[CAN_COMMAND_INTERFACE_TYPE]["interfaceName"].asStringRequired(),
                    mRawDataBufferManager.get() );
                if ( !mActuatorCommandManager->registerDispatcher( interfaceId, mCanCommandDispatcher ) )
                {
                    return false;
                }
            }
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
            else if ( interfaceType == AAOS_VHAL_INTERFACE_TYPE )
            {
                if ( mAaosVhalSource != nullptr )
                {
                    continue;
                }
                mAaosVhalSource = std::make_shared<AaosVhalSource>( interfaceId, mSignalBufferDistributor );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &AaosVhalSource::onChangeOfActiveDictionary,
                               mAaosVhalSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
            }
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
            else if ( interfaceType == IWAVE_GPS_INTERFACE_TYPE )
            {
                if ( mIWaveGpsSource != nullptr )
                {
                    continue;
                }
                // coverity[autosar_cpp14_a20_8_4_violation] Shared pointer interface required for unit testing
                auto namedSignalDataSource =
                    std::make_shared<NamedSignalDataSource>( interfaceId, mSignalBufferDistributor );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &NamedSignalDataSource::onChangeOfActiveDictionary,
                               namedSignalDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
                auto iwaveGpsConfig = networkInterfaceConfig[IWAVE_GPS_INTERFACE_TYPE];
                mIWaveGpsSource = std::make_shared<IWaveGpsSource>(
                    namedSignalDataSource,
                    iwaveGpsConfig[IWaveGpsSource::PATH_TO_NMEA].asStringRequired(),
                    iwaveGpsConfig[IWaveGpsSource::LATITUDE_SIGNAL_NAME].asStringRequired(),
                    iwaveGpsConfig[IWaveGpsSource::LONGITUDE_SIGNAL_NAME].asStringRequired(),
                    iwaveGpsConfig[IWaveGpsSource::POLL_INTERVAL_MS].asU32Required() );
                if ( !mIWaveGpsSource->connect() )
                {
                    FWE_LOG_ERROR( "IWaveGps initialization failed" );
                    return false;
                }
            }
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
            else if ( interfaceType == EXTERNAL_GPS_INTERFACE_TYPE )
            {
                if ( mExternalGpsSource != nullptr )
                {
                    continue;
                }
                // coverity[autosar_cpp14_a20_8_4_violation] Shared pointer interface required for unit testing
                auto namedSignalDataSource =
                    std::make_shared<NamedSignalDataSource>( interfaceId, mSignalBufferDistributor );
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                mCollectionSchemeManagerPtr->subscribeToActiveDecoderDictionaryChange(
                    std::bind( &NamedSignalDataSource::onChangeOfActiveDictionary,
                               namedSignalDataSource.get(),
                               std::placeholders::_1,
                               std::placeholders::_2 ) );
                auto externalGpsConfig = networkInterfaceConfig[EXTERNAL_GPS_INTERFACE_TYPE];
                mExternalGpsSource = std::make_shared<ExternalGpsSource>(
                    namedSignalDataSource,
                    externalGpsConfig[ExternalGpsSource::LATITUDE_SIGNAL_NAME].asStringRequired(),
                    externalGpsConfig[ExternalGpsSource::LONGITUDE_SIGNAL_NAME].asStringRequired() );
            }
#endif
            else if ( ( mNetworkInterfaceConfigHook != nullptr ) &&
                      mNetworkInterfaceConfigHook( networkInterfaceConfig ) )
            {
                // External interface was configured
            }
            else
            {
                FWE_LOG_ERROR( interfaceType + " is not supported" );
            }
        }
#ifdef FWE_FEATURE_UDS_DTC
        mDiagnosticDataSource = std::make_shared<RemoteDiagnosticDataSource>( mDiagnosticNamedSignalDataSource,
                                                                              mRawDataBufferManager.get()
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
                                                                                  ,
                                                                              mExampleDiagnosticInterface
#endif
        );
        if ( !mDiagnosticDataSource->start() )
        {
            FWE_LOG_ERROR( "Failed to start the Remote Diagnostics Data Source" );
            return false;
        }
        mDataFetchManager->registerCustomFetchFunction( "DTC_QUERY",
                                                        std::bind( &RemoteDiagnosticDataSource::DTC_QUERY,
                                                                   mDiagnosticDataSource.get(),
                                                                   std::placeholders::_1,
                                                                   std::placeholders::_2,
                                                                   std::placeholders::_3 ) );
#endif
/********************************Data source bootstrap end*******************************/

/*******************************Custom function setup begin******************************/
#ifdef FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES
        mCollectionInspectionEngine->registerCustomFunction( "abs", { CustomFunctionMath::absFunc, nullptr, nullptr } );
        mCollectionInspectionEngine->registerCustomFunction( "min", { CustomFunctionMath::minFunc, nullptr, nullptr } );
        mCollectionInspectionEngine->registerCustomFunction( "max", { CustomFunctionMath::maxFunc, nullptr, nullptr } );
        mCollectionInspectionEngine->registerCustomFunction( "pow", { CustomFunctionMath::powFunc, nullptr, nullptr } );
        mCollectionInspectionEngine->registerCustomFunction( "log", { CustomFunctionMath::logFunc, nullptr, nullptr } );
        mCollectionInspectionEngine->registerCustomFunction( "ceil",
                                                             { CustomFunctionMath::ceilFunc, nullptr, nullptr } );
        mCollectionInspectionEngine->registerCustomFunction( "floor",
                                                             { CustomFunctionMath::floorFunc, nullptr, nullptr } );

        mCustomFunctionMultiRisingEdgeTrigger = std::make_unique<CustomFunctionMultiRisingEdgeTrigger>(
            mNamedSignalDataSource, mRawDataBufferManager.get() );
        mCollectionInspectionEngine->registerCustomFunction(
            "MULTI_RISING_EDGE_TRIGGER",
            { [this]( auto invocationId, const auto &args ) -> CustomFunctionInvokeResult {
                 return mCustomFunctionMultiRisingEdgeTrigger->invoke( invocationId, args );
             },
              [this]( const auto &collectedSignalIds, auto timestamp, auto &collectedData ) {
                  mCustomFunctionMultiRisingEdgeTrigger->conditionEnd( collectedSignalIds, timestamp, collectedData );
              },
              [this]( auto invocationId ) {
                  mCustomFunctionMultiRisingEdgeTrigger->cleanup( invocationId );
              } } );
#endif
#ifdef FWE_FEATURE_SCRIPT_ENGINE
        if ( ( mAwsCredentialsProvider == nullptr ) || ( !config["staticConfig"].isMember( "scriptEngine" ) ) )
        {
            FWE_LOG_TRACE( "Script engine support is disabled. Add 'credentialsProvider' and 'scriptEngine' section to "
                           "the config to initialize it." );
        }
        else
        {
            auto bucketRegion = config["staticConfig"]["scriptEngine"]["bucketRegion"].asStringRequired();
            auto s3MaxConnections = config["staticConfig"]["scriptEngine"]["maxConnections"].asU32Required();
            s3MaxConnections = s3MaxConnections > 0U ? s3MaxConnections : 1U;

            auto transferManagerConfiguration =
                std::make_shared<Aws::Transfer::TransferManagerConfiguration>( nullptr );

            Aws::S3::Model::GetObjectRequest getObjectTemplate;
            getObjectTemplate.WithExpectedBucketOwner(
                config["staticConfig"]["scriptEngine"]["bucketOwner"].asStringRequired() );
            transferManagerConfiguration->getObjectTemplate = getObjectTemplate;

            transferManagerConfiguration->transferStatusUpdatedCallback =
                // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
                [this]( const Aws::Transfer::TransferManager *transferManager,
                        const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle ) {
                    static_cast<void>( transferManager );
                    mCustomFunctionScriptEngine->transferStatusUpdatedCallback( transferHandle );
                };
            transferManagerConfiguration->errorCallback =
                // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
                [this]( const Aws::Transfer::TransferManager *transferManager,
                        const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle,
                        const Aws::Client::AWSError<Aws::S3::S3Errors> &error ) {
                    static_cast<void>( transferManager );
                    mCustomFunctionScriptEngine->transferErrorCallback( transferHandle, error );
                };
            transferManagerConfiguration->transferInitiatedCallback =
                // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
                [this]( const Aws::Transfer::TransferManager *transferManager,
                        const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle ) {
                    static_cast<void>( transferManager );
                    mCustomFunctionScriptEngine->transferInitiatedCallback( transferHandle );
                };
            transferManagerConfiguration->transferExecutor = getTransferManagerExecutor().get();

            auto createTransferManagerWrapper = [this,
                                                 transferManagerConfiguration,
                                                 bucketRegion,
                                                 s3MaxConnections]() -> std::shared_ptr<TransferManagerWrapper> {
                Aws::Client::ClientConfigurationInitValues initValues;
                // The SDK can use IMDS to determine the region, but since we will pass the region we don't
                // want the SDK to use it, specially because in non-EC2 environments without any AWS SDK
                // config at all, this can cause delays when setting up the client:
                // https://github.com/aws/aws-sdk-cpp/issues/1511
                initValues.shouldDisableIMDS = true;
                Aws::Client::ClientConfiguration clientConfiguration( initValues );
                clientConfiguration.region = bucketRegion;
                clientConfiguration.maxConnections = s3MaxConnections;
                auto s3Client =
                    std::make_shared<Aws::S3::S3Client>( mAwsCredentialsProvider,
                                                         Aws::MakeShared<Aws::S3::S3EndpointProvider>( "S3Client" ),
                                                         clientConfiguration );
                transferManagerConfiguration->s3Client = s3Client;
                return std::make_shared<TransferManagerWrapper>(
                    Aws::Transfer::TransferManager::Create( *transferManagerConfiguration ) );
            };

            mCustomFunctionScriptEngine = std::make_shared<CustomFunctionScriptEngine>(
                mNamedSignalDataSource,
                mRawDataBufferManager.get(),
                createTransferManagerWrapper,
                getAbsolutePath( config["staticConfig"]["persistency"]["persistencyPath"].asStringRequired() +
                                     "/scripts",
                                 configFileDirectoryPath )
                    .string(),
                config["staticConfig"]["scriptEngine"]["bucketName"].asStringRequired() );
        }
#endif
#ifdef FWE_FEATURE_MICROPYTHON
        if ( ( mCustomFunctionScriptEngine == nullptr ) || ( !config["staticConfig"].isMember( "micropython" ) ) )
        {
            FWE_LOG_TRACE( "MicroPython support is disabled. Add 'scriptEngine' and 'micropython' section to "
                           "the config to initialize it." );
        }
        else
        {
            mCustomFunctionMicroPython = std::make_unique<CustomFunctionMicroPython>( mCustomFunctionScriptEngine );
            mCollectionInspectionEngine->registerCustomFunction(
                "python",
                CustomFunctionCallbacks{
                    [this]( auto invocationID, const auto &args ) -> CustomFunctionInvokeResult {
                        return mCustomFunctionMicroPython->invoke( invocationID, args );
                    },
                    // coverity[autosar_cpp14_a5_1_9_violation] Duplicate lambda for ease of maintenance
                    [this]( const auto &collectedSignalIds, auto timestamp, auto &collectedData ) {
                        mCustomFunctionScriptEngine->conditionEnd( collectedSignalIds, timestamp, collectedData );
                    },
                    [this]( auto invocationID ) {
                        mCustomFunctionMicroPython->cleanup( invocationID );
                    } } );
        }
#endif
#ifdef FWE_FEATURE_CPYTHON
        if ( ( mCustomFunctionScriptEngine == nullptr ) || ( !config["staticConfig"].isMember( "cpython" ) ) )
        {
            FWE_LOG_TRACE( "CPython support is disabled. Add 'scriptEngine' and 'cpython' section to "
                           "the config to initialize it." );
        }
        else
        {
            mCustomFunctionCPython = std::make_unique<CustomFunctionCPython>( mCustomFunctionScriptEngine );
            mCollectionInspectionEngine->registerCustomFunction(
                "python",
                CustomFunctionCallbacks{
                    [this]( auto invocationID, const auto &args ) -> CustomFunctionInvokeResult {
                        return mCustomFunctionCPython->invoke( invocationID, args );
                    },
                    // coverity[autosar_cpp14_a5_1_9_violation] Duplicate lambda for ease of maintenance
                    [this]( const auto &collectedSignalIds, auto timestamp, auto &collectedData ) {
                        mCustomFunctionScriptEngine->conditionEnd( collectedSignalIds, timestamp, collectedData );
                    },
                    [this]( auto invocationID ) {
                        mCustomFunctionCPython->cleanup( invocationID );
                    } } );
        }
#endif
        /********************************Custom function setup end*******************************/

#ifdef FWE_FEATURE_REMOTE_COMMANDS
        /********************************Remote commands bootstrap begin***************************/
        if ( receiverCommandRequest )
        {
            mCommandSchema = std::make_unique<CommandSchema>(
                *receiverCommandRequest, mCommandResponses, mRawDataBufferManager.get() );
            if ( receiverRejectedCommandResponse != nullptr )
            {
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                receiverRejectedCommandResponse->subscribeToDataReceived(
                    std::bind( &CommandSchema::onRejectedCommandResponseReceived, std::placeholders::_1 ) );
            }
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCommandResponses->subscribeToNewDataAvailable( std::bind(
                &DataSenderManagerWorkerThread::onDataReadyToPublish, mDataSenderManagerWorkerThread.get() ) );
            if ( !mActuatorCommandManager->start() )
            {
                FWE_LOG_ERROR( "Failed to init and start the Command Manager" );
                return false;
            }
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCollectionSchemeManagerPtr->subscribeToCustomSignalDecoderFormatMapChange(
                std::bind( &ActuatorCommandManager::onChangeOfCustomSignalDecoderFormatMap,
                           mActuatorCommandManager.get(),
                           std::placeholders::_1,
                           std::placeholders::_2 ) );
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            mCommandSchema->subscribeToActuatorCommandRequestReceived(
                std::bind( &ActuatorCommandManager::onReceivingCommandRequest,
                           mActuatorCommandManager.get(),
                           std::placeholders::_1 ) );
        }

        /********************************Remote commands bootstrap end*****************************/
#endif

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        /********************************Last known state bootstrap begin**************************/
        if ( receiverCommandRequest && receiverLastKnownStateConfig )
        {
            auto lastKnownStateInspector =
                std::make_unique<LastKnownStateInspector>( mCommandResponses, mCacheAndPersist );
            auto lastKnownStateSignalBuffer =
                std::make_shared<SignalBuffer>( signalBufferSize,
                                                "LKS Signal Buffer",
                                                TraceAtomicVariable::QUEUE_CONSUMER_TO_LAST_KNOWN_STATE_INSPECTION,
                                                // Notify listeners when 10% of the buffer is full so that we don't
                                                // let it grow too much.
                                                signalBufferSize / 10 );

            mSignalBufferDistributor.registerQueue( lastKnownStateSignalBuffer );
            mLastKnownStateWorkerThread = std::make_shared<LastKnownStateWorkerThread>(
                lastKnownStateSignalBuffer,
                mLastKnownStateDataReadyToPublish,
                std::move( lastKnownStateInspector ),
                config["staticConfig"]["threadIdleTimes"]["lastKnownStateThreadIdleTimeMs"]
                    .asU32Optional()
                    .get_value_or( 0 ) );
            mCollectionSchemeManagerPtr->subscribeToStateTemplatesChange(
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                std::bind( &LastKnownStateWorkerThread::onStateTemplatesChanged,
                           mLastKnownStateWorkerThread.get(),
                           std::placeholders::_1 ) );
            mCommandSchema->subscribeToLastKnownStateCommandRequestReceived(
                // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
                std::bind( &LastKnownStateWorkerThread::onNewCommandReceived,
                           mLastKnownStateWorkerThread.get(),
                           std::placeholders::_1 ) );
            // coverity[autosar_cpp14_a18_9_1_violation] std::bind is easier to maintain than extra lambda
            lastKnownStateSignalBuffer->subscribeToNewDataAvailable(
                std::bind( &LastKnownStateWorkerThread::onNewDataAvailable, mLastKnownStateWorkerThread.get() ) );

            if ( !mLastKnownStateWorkerThread->start() )
            {
                FWE_LOG_ERROR( "Failed to init and start the Last Known State Inspection Engine" );
                return false;
            }
        }
        else if ( receiverLastKnownStateConfig == nullptr )
        {
            FWE_LOG_INFO( "Disabling LastKnownState because LastKnownState topics are not configured" );
        }
        else if ( receiverCommandRequest == nullptr )
        {
            FWE_LOG_WARN( "Disabling LastKnownState because command topics are not configured" );
        }
        /********************************Last known state bootstrap end****************************/
#endif

        if ( mStartupConfigHook != nullptr )
        {
            mStartupConfigHook( config );
        }

        if ( !mCollectionInspectionWorkerThread->start() )
        {
            FWE_LOG_ERROR( "Failed to start the Inspection Engine" );
            return false;
        }

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

        if ( !mDataFetchManager->start() )
        {
            FWE_LOG_ERROR( "Failed to start the DataFetchManager" );
            return false;
        }

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
    if ( mShutdownConfigHook != nullptr )
    {
        if ( !mShutdownConfigHook() )
        {
            return false;
        }
    }
#ifdef FWE_FEATURE_AAOS_VHAL
    mAaosVhalSource.reset();
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
    mExternalGpsSource.reset();
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
    mIWaveGpsSource.reset();
#endif
#ifdef FWE_FEATURE_ROS2
    if ( mROS2DataSource )
    {
        mROS2DataSource->disconnect();
    }
#endif
#ifdef FWE_FEATURE_SOMEIP
    for ( auto &bridge : mSomeipToCanBridges )
    {
        bridge->disconnect();
    }
    mSomeipDataSource.reset();
    mExampleSomeipCommandDispatcher.reset();
    if ( mDeviceShadowOverSomeip )
    {
        if ( !CommonAPI::Runtime::get()->unregisterService(
                 "local",
                 v1::commonapi::DeviceShadowOverSomeipInterface::getInterface(),
                 mDeviceShadowOverSomeipInstanceName ) )
        {
            FWE_LOG_ERROR( "Failed to unregister DeviceShadowOverSomeip service" );
            return false;
        }
    }
    mDeviceShadowOverSomeip.reset();
#endif

    if ( mOBDOverCANModule )
    {
        if ( !mOBDOverCANModule->disconnect() )
        {
            FWE_LOG_ERROR( "Could not disconnect OBD over CAN module" );
            return false;
        }
    }

#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
    if ( mExampleDiagnosticInterface != nullptr )
    {
        if ( !mExampleDiagnosticInterface->stop() )
        {
            FWE_LOG_ERROR( "Could not stop DiagnosticInterface" );
            return false;
        }
    }
#endif
#ifdef FWE_FEATURE_UDS_DTC
    if ( mDiagnosticDataSource != nullptr )
    {
        if ( !mDiagnosticDataSource->stop() )
        {
            FWE_LOG_ERROR( "Could not stop DiagnosticDataSource" );
            return false;
        }
    }
#endif
    if ( !mDataFetchManager->stop() )
    {
        FWE_LOG_ERROR( "Could not stop the DataFetchManager" );
        return false;
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

#ifdef FWE_FEATURE_REMOTE_COMMANDS
    if ( mActuatorCommandManager != nullptr )
    {
        if ( !mActuatorCommandManager->stop() )
        {
            FWE_LOG_ERROR( "Could not stop the ActuatorCommandManager" );
            return false;
        }
    }
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    // iot jobs depends on stream forwarder,
    // so only stop forwarder after connectivity module is disconnected
    if ( mStreamForwarder )
    {
        if ( !mStreamForwarder->stop() )
        {
            FWE_LOG_ERROR( "Could not stop the SteamForwarder" );
            return false;
        }
    }
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    if ( ( mLastKnownStateWorkerThread != nullptr ) && ( !mLastKnownStateWorkerThread->stop() ) )
    {
        FWE_LOG_ERROR( "Could not stop the Last Known State Inspection Thread" );
        return false;
    }
#endif
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

#ifdef FWE_FEATURE_SCRIPT_ENGINE
    if ( mCustomFunctionScriptEngine )
    {
        mCustomFunctionScriptEngine->shutdown();
    }
#endif
#ifdef FWE_FEATURE_MICROPYTHON
    mCustomFunctionMicroPython.reset();
#endif
#ifdef FWE_FEATURE_CPYTHON
    mCustomFunctionCPython.reset();
#endif
#ifdef FWE_FEATURE_SCRIPT_ENGINE
    mCustomFunctionScriptEngine.reset();
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
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
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
IoTFleetWiseEngine::doWork()
{
    TraceModule::get().sectionEnd( TraceSection::FWE_STARTUP );

    while ( !shouldStop() )
    {
        mTimer.reset();
        uint64_t minTimeToWaitMs = UINT64_MAX;
        if ( mPrintMetricsCyclicPeriodMs != 0 )
        {
            uint64_t timeToWaitMs = mPrintMetricsCyclicPeriodMs -
                                    std::min( static_cast<uint64_t>( mPrintMetricsCyclicTimer.getElapsedMs().count() ),
                                              mPrintMetricsCyclicPeriodMs );
            minTimeToWaitMs = std::min( minTimeToWaitMs, timeToWaitMs );
        }
        if ( minTimeToWaitMs < UINT64_MAX )
        {
            FWE_LOG_TRACE( "Waiting for: " + std::to_string( minTimeToWaitMs ) + " ms. Cyclic metrics print:" +
                           std::to_string( mPrintMetricsCyclicPeriodMs ) + " configured,  " +
                           std::to_string( mPrintMetricsCyclicTimer.getElapsedMs().count() ) + " timer." );
            mWait.wait( static_cast<uint32_t>( minTimeToWaitMs ) );
        }
        else
        {
            mWait.wait( Signal::WaitWithPredicate );
            auto elapsedTimeMs = mTimer.getElapsedMs().count();
            FWE_LOG_TRACE( "Event arrived. Time elapsed waiting for the event: " + std::to_string( elapsedTimeMs ) +
                           " ms" );
        }
        if ( ( mPrintMetricsCyclicPeriodMs > 0 ) &&
             ( static_cast<uint64_t>( mPrintMetricsCyclicTimer.getElapsedMs().count() ) >=
               mPrintMetricsCyclicPeriodMs ) )
        {
            mPrintMetricsCyclicTimer.reset();
            TraceModule::get().print();
            TraceModule::get().startNewObservationWindow( static_cast<uint32_t>( mPrintMetricsCyclicPeriodMs ) );
        }
    }
}

std::string
IoTFleetWiseEngine::getStatusSummary()
{
    if ( mConnectivityModule == nullptr || mCollectionSchemeManagerPtr == nullptr || mMqttSender == nullptr ||
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

    status += "Payloads sent: " + std::to_string( mMqttSender->getPayloadCountSent() ) + "\n\n";
    return status;
}

} // namespace IoTFleetWise
} // namespace Aws
