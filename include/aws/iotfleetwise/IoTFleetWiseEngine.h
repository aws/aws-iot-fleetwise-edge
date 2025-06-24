// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CANDataConsumer.h"
#include "aws/iotfleetwise/CANDataSource.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CollectionInspectionEngine.h"
#include "aws/iotfleetwise/CollectionInspectionWorkerThread.h"
#include "aws/iotfleetwise/CollectionSchemeManager.h"
#include "aws/iotfleetwise/DataFetchManager.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/DataSenderManagerWorkerThread.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ExternalCANDataSource.h"
#include "aws/iotfleetwise/IConnectivityModule.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/IoTFleetWiseConfig.h"
#include "aws/iotfleetwise/MqttClientWrapper.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/OBDOverCANModule.h"
#include "aws/iotfleetwise/PayloadManager.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/RemoteProfiler.h"
#include "aws/iotfleetwise/Schema.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/Timer.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include <atomic>
#include <boost/filesystem.hpp>
#include <csignal>
#include <cstdint>
#include <functional>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef FWE_FEATURE_UDS_DTC
#include "aws/iotfleetwise/RemoteDiagnosticDataSource.h"
#endif
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
#include "aws/iotfleetwise/ExampleUDSInterface.h"
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
#include "aws/iotfleetwise/AaosVhalSource.h"
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
#include "aws/iotfleetwise/ExternalGpsSource.h"
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
#include "aws/iotfleetwise/IWaveGpsSource.h"
#endif
#ifdef FWE_FEATURE_S3
#include <aws/core/VersionConfig.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#if ( AWS_SDK_VERSION_MAJOR > 1 ) ||                                                                                   \
    ( ( AWS_SDK_VERSION_MAJOR == 1 ) &&                                                                                \
      ( ( AWS_SDK_VERSION_MINOR > 11 ) || ( ( AWS_SDK_VERSION_MINOR == 11 ) && ( AWS_SDK_VERSION_PATCH >= 224 ) ) ) )
#include <aws/core/utils/threading/PooledThreadExecutor.h>
#else
#include <aws/core/utils/threading/Executor.h>
#endif
#endif
#ifdef FWE_FEATURE_GREENGRASSV2
#include "aws/iotfleetwise/AwsGreengrassCoreIpcClientWrapper.h"
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#endif
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "aws/iotfleetwise/S3Sender.h"
#endif
#ifdef FWE_FEATURE_ROS2
#include "aws/iotfleetwise/ROS2DataSource.h"
#endif
#ifdef FWE_FEATURE_SOMEIP
#include "aws/iotfleetwise/DeviceShadowOverSomeip.h"
#include "aws/iotfleetwise/SomeipCommandDispatcher.h"
#include "aws/iotfleetwise/SomeipDataSource.h"
#include "aws/iotfleetwise/SomeipToCanBridge.h"
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
#include "aws/iotfleetwise/ActuatorCommandManager.h"
#include "aws/iotfleetwise/CanCommandDispatcher.h"
#include "aws/iotfleetwise/CommandSchema.h"
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "aws/iotfleetwise/LastKnownStateSchema.h"
#include "aws/iotfleetwise/LastKnownStateWorkerThread.h"
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "aws/iotfleetwise/snf/IoTJobsDataRequestHandler.h"
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include "aws/iotfleetwise/snf/StreamManager.h"
#endif
#ifdef FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES
#include "aws/iotfleetwise/CustomFunctionMultiRisingEdgeTrigger.h"
#endif
#ifdef FWE_FEATURE_SCRIPT_ENGINE
#include "aws/iotfleetwise/CustomFunctionScriptEngine.h"
#endif
#ifdef FWE_FEATURE_MICROPYTHON
#include "aws/iotfleetwise/CustomFunctionMicroPython.h"
#endif
#ifdef FWE_FEATURE_CPYTHON
#include "aws/iotfleetwise/CustomFunctionCPython.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Main AWS IoT FleetWise Bootstrap module.
 * 1- Initializes the Connectivity Module
 * 2- Initializes the Inspection Engine
 * 3- Initializes the CollectionScheme Ingestion & Management modules
 * 5- Initializes the Vehicle Network module.
 * 6- Initializes the DataSenderManager module.
 */
class IoTFleetWiseEngine
{
public:
    IoTFleetWiseEngine();
    ~IoTFleetWiseEngine();

    IoTFleetWiseEngine( const IoTFleetWiseEngine & ) = delete;
    IoTFleetWiseEngine &operator=( const IoTFleetWiseEngine & ) = delete;
    IoTFleetWiseEngine( IoTFleetWiseEngine && ) = delete;
    IoTFleetWiseEngine &operator=( IoTFleetWiseEngine && ) = delete;

    static void configureSignalHandlers();
    static std::string getVersion();
    static void configureLogging( const Json::Value &config );
    static int signalToExitCode( int signalNumber );

    bool connect( const Json::Value &jsonConfig, const boost::filesystem::path &configFileDirectoryPath );
    bool start();
    bool stop();
    bool disconnect();
    bool isAlive();

    /**
     * @brief Return a status summary, including the MQTT connection status, the campaign ARNs,
     *        and the number of payloads sent.
     * @return String with the status summary
     */
    std::string getStatusSummary();

#ifdef FWE_FEATURE_GREENGRASSV2
    std::unique_ptr<Aws::Greengrass::GreengrassCoreIpcClient> mGreengrassClient;
    std::unique_ptr<AwsGreengrassCoreIpcClientWrapper> mGreengrassClientWrapper;
#endif

    std::shared_ptr<IConnectivityModule> mConnectivityModule;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::unique_ptr<RawData::BufferManager> mRawDataBufferManager;
    std::unique_ptr<ExternalCANDataSource> mExternalCANDataSource;
    std::shared_ptr<OBDOverCANModule> mOBDOverCANModule;
#ifdef FWE_FEATURE_S3
    std::shared_ptr<Aws::Auth::AWSCredentialsProvider> mAwsCredentialsProvider;
    std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> getTransferManagerExecutor();
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
    std::shared_ptr<ExternalGpsSource> mExternalGpsSource;
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
    std::shared_ptr<AaosVhalSource> mAaosVhalSource;
#endif

    /**
     * Set connectivity module config hook. Hook should return true if the connectivity module was
     * configured and mConnectivityModule is set.
     * @note Should be set before `connect` is called.
     * @param hook Config hook function
     */
    void
    setConnectivityModuleConfigHook( std::function<bool( const IoTFleetWiseConfig &config )> hook )
    {
        mConnectivityModuleConfigHook = std::move( hook );
    }

    /**
     * Set network interface config hook. Hook should return true if the interface was configured.
     * @note Should be set before `connect` is called.
     * @param hook Config hook function
     */
    void
    setNetworkInterfaceConfigHook( std::function<bool( const IoTFleetWiseConfig &networkInterfaceConfig )> hook )
    {
        mNetworkInterfaceConfigHook = std::move( hook );
    }

    /**
     * Set startup config hook. Can be used to configure data sources using the NamedSignalDataSource, custom functions
     * and actuator dispatchers.
     * @note Should be set before `connect` is called.
     * @param hook Config hook function
     */
    void
    setStartupConfigHook( std::function<void( const IoTFleetWiseConfig &config )> hook )
    {
        mStartupConfigHook = std::move( hook );
    }

    /**
     * Set shutdown config hook. Can be used to stop data sources.
     * @note Should be set before `connect` is called.
     * @param hook Config hook function
     */
    void
    setShutdownConfigHook( std::function<bool()> hook )
    {
        mShutdownConfigHook = std::move( hook );
    }

    SignalBufferDistributor mSignalBufferDistributor;
    std::shared_ptr<CollectionSchemeManager> mCollectionSchemeManagerPtr;
    std::shared_ptr<CollectionInspectionEngine> mCollectionInspectionEngine;
#ifdef FWE_FEATURE_SCRIPT_ENGINE
    std::shared_ptr<CustomFunctionScriptEngine> mCustomFunctionScriptEngine;
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    std::shared_ptr<ActuatorCommandManager> mActuatorCommandManager;
    std::shared_ptr<CanCommandDispatcher> mCanCommandDispatcher;
#endif

private:
    bool shouldStop() const;
    void doWork();
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;
    Signal mWait;
    Timer mTimer;
    Timer mPrintMetricsCyclicTimer;
    uint64_t mPrintMetricsCyclicPeriodMs{ 0 }; // default to 0 which means no cyclic printing
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    std::shared_ptr<ISender> mMqttSender;
    std::shared_ptr<CacheAndPersist> mCacheAndPersist;
    CANInterfaceIDTranslator mCANIDTranslator;
    std::vector<std::unique_ptr<CANDataSource>> mCANDataSources;
    std::unique_ptr<CANDataConsumer> mCANDataConsumer;
    std::unique_ptr<TopicConfig> mTopicConfig;
    std::unique_ptr<MqttClientBuilderWrapper> mBuilderWrapper;
    std::shared_ptr<IReceiver> mReceiverCollectionSchemeList;
    std::shared_ptr<IReceiver> mReceiverDecoderManifest;
    std::shared_ptr<PayloadManager> mPayloadManager;
    std::shared_ptr<FetchRequestQueue> mFetchQueue;
    std::function<bool( const IoTFleetWiseConfig &config )> mConnectivityModuleConfigHook;
    std::function<bool( const IoTFleetWiseConfig &networkInterfaceConfig )> mNetworkInterfaceConfigHook;
    std::function<void( const IoTFleetWiseConfig &config )> mStartupConfigHook;
    std::function<bool()> mShutdownConfigHook;

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    bool mStoreAndForwardEnabled = true;
    std::unique_ptr<Aws::IoTFleetWise::Store::StreamManager> mStreamManager;
    std::unique_ptr<RateLimiter> mRateLimiter;
    std::unique_ptr<Aws::IoTFleetWise::Store::StreamForwarder> mStreamForwarder;
#endif
#ifdef FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES
    std::unique_ptr<CustomFunctionMultiRisingEdgeTrigger> mCustomFunctionMultiRisingEdgeTrigger;
#endif
#ifdef FWE_FEATURE_MICROPYTHON
    std::unique_ptr<CustomFunctionMicroPython> mCustomFunctionMicroPython;
#endif
#ifdef FWE_FEATURE_CPYTHON
    std::unique_ptr<CustomFunctionCPython> mCustomFunctionCPython;
#endif

    std::shared_ptr<Schema> mSchemaPtr;
    std::shared_ptr<CheckinSender> mCheckinSender;
    std::shared_ptr<CollectionInspectionWorkerThread> mCollectionInspectionWorkerThread;
    std::shared_ptr<DataSenderQueue> mCollectedDataReadyToPublish;
    std::unordered_map<SenderDataType, std::unique_ptr<DataSender>> mDataSenders;
    std::shared_ptr<DataSenderManagerWorkerThread> mDataSenderManagerWorkerThread;
    std::shared_ptr<DataFetchManager> mDataFetchManager;
    std::unique_ptr<RemoteProfiler> mRemoteProfiler;
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
    std::shared_ptr<ExampleUDSInterface> mExampleDiagnosticInterface;
#endif
#ifdef FWE_FEATURE_UDS_DTC
    std::shared_ptr<RemoteDiagnosticDataSource> mDiagnosticDataSource;
    std::shared_ptr<NamedSignalDataSource> mDiagnosticNamedSignalDataSource;
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    std::shared_ptr<DataSenderQueue> mCommandResponses;
    std::unique_ptr<CommandSchema> mCommandSchema;
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    std::shared_ptr<DataSenderQueue> mLastKnownStateDataReadyToPublish;
    std::unique_ptr<LastKnownStateSchema> mLastKnownStateSchema;
    std::shared_ptr<LastKnownStateWorkerThread> mLastKnownStateWorkerThread;
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    std::shared_ptr<IReceiver> mReceiverIotJob;
    std::shared_ptr<IReceiver> mReceiverJobDocumentAccepted;
    std::shared_ptr<IReceiver> mReceiverJobDocumentRejected;
    std::shared_ptr<IReceiver> mReceiverPendingJobsAccepted;
    std::shared_ptr<IReceiver> mReceiverPendingJobsRejected;
    std::shared_ptr<IReceiver> mReceiverUpdateIotJobStatusAccepted;
    std::shared_ptr<IReceiver> mReceiverUpdateIotJobStatusRejected;
    std::shared_ptr<IReceiver> mReceiverCanceledIoTJobs;
    std::unique_ptr<IoTJobsDataRequestHandler> mIoTJobsDataRequestHandler;
#endif
#ifdef FWE_FEATURE_S3
    std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> mTransferManagerExecutor;
    std::mutex mTransferManagerExecutorMutex;
#endif
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::unique_ptr<S3Sender> mS3Sender;
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
    std::shared_ptr<IWaveGpsSource> mIWaveGpsSource;
#endif
#ifdef FWE_FEATURE_ROS2
    std::shared_ptr<ROS2DataSource> mROS2DataSource;
#endif
#ifdef FWE_FEATURE_SOMEIP
    std::unique_ptr<SomeipDataSource> mSomeipDataSource;
    std::vector<std::unique_ptr<SomeipToCanBridge>> mSomeipToCanBridges;
    // Create one for each SOME/IP interface
    std::shared_ptr<SomeipCommandDispatcher> mExampleSomeipCommandDispatcher;

    std::shared_ptr<DeviceShadowOverSomeip> mDeviceShadowOverSomeip;
    std::string mDeviceShadowOverSomeipInstanceName;
#endif
};

/// Global signal variable, set when configureSignalHandlers is used
// coverity[autosar_cpp14_a2_10_4_violation:FALSE] False positive, this variable is declared only once.
// coverity[autosar_cpp14_a2_11_1_violation] volatile required as it will be modified by a signal handler
extern volatile sig_atomic_t gSignal; // NOLINT Global signal

} // namespace IoTFleetWise
} // namespace Aws
