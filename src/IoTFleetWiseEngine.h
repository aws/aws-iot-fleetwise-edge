// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANDataConsumer.h"
#include "CANDataSource.h"
#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "CheckinSender.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionEngine.h"
#include "CollectionInspectionWorkerThread.h"
#include "CollectionSchemeManager.h"
#include "DataFetchManager.h"
#include "DataSenderManager.h"
#include "DataSenderManagerWorkerThread.h"
#include "DataSenderTypes.h"
#include "ExternalCANDataSource.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "ISender.h"
#include "NamedSignalDataSource.h"
#include "OBDDataTypes.h"
#include "OBDOverCANModule.h"
#include "PayloadManager.h"
#include "RawDataManager.h"
#include "RemoteProfiler.h"
#include "Schema.h"
#include "Signal.h"
#include "SignalTypes.h"
#include "Thread.h"
#include "TimeTypes.h"
#include "Timer.h"
#include "TopicConfig.h"
#include <atomic>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef FWE_FEATURE_UDS_DTC
#include "RemoteDiagnosticDataSource.h"
#endif
#ifdef FWE_FEATURE_UDS_DTC_EXAMPLE
#include "ExampleUDSInterface.h"
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
#include "AaosVhalSource.h"
#include <array>
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
#include "ExternalGpsSource.h"
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
#include "IWaveGpsSource.h"
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
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "S3Sender.h"
#endif
#ifdef FWE_FEATURE_ROS2
#include "ROS2DataSource.h"
#endif
#ifdef FWE_FEATURE_SOMEIP
#include "DeviceShadowOverSomeip.h"
#include "SomeipCommandDispatcher.h"
#include "SomeipDataSource.h"
#include "SomeipToCanBridge.h"
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
#include "ActuatorCommandManager.h"
#include "CanCommandDispatcher.h"
#include "CommandSchema.h"
#endif
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "LastKnownStateSchema.h"
#include "LastKnownStateWorkerThread.h"
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "IoTJobsDataRequestHandler.h"
#include "StreamForwarder.h"
#include "StreamManager.h"
#endif
#ifdef FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES
#include "CustomFunctionMultiRisingEdgeTrigger.h"
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

    bool connect( const Json::Value &jsonConfig, const boost::filesystem::path &configFileDirectoryPath );
    bool start();
    bool stop();
    bool disconnect();
    bool isAlive();

    /**
     * @brief Gets a list of OBD PIDs to request externally
     */
    std::vector<PID> getExternalOBDPIDsToRequest();

    /**
     * @brief Sets the OBD response for the given PID
     * @param pid The OBD PID
     * @param response The OBD response
     */
    void setExternalOBDPIDResponse( PID pid, const std::vector<uint8_t> &response );

    /** Ingest a CAN message from an external source
     * @param interfaceId Interface identifier
     * @param timestamp Timestamp of CAN message in milliseconds since epoch, or zero if unknown.
     * @param messageId CAN message ID in Linux SocketCAN format
     * @param data CAN message data */
    void ingestExternalCANMessage( const InterfaceID &interfaceId,
                                   Timestamp timestamp,
                                   uint32_t messageId,
                                   const std::vector<uint8_t> &data );

    /** @brief Ingest a signal value by name
     * @param timestamp Timestamp of signal value in milliseconds since epoch, or zero if unknown.
     * @param name Signal name
     * @param value Signal value */
    void ingestSignalValueByName( Timestamp timestamp, const std::string &name, const DecodedSignalValue &value );

    /** @brief Ingest multiple signal values by name
     * @param timestamp Timestamp of signal values in milliseconds since epoch, or zero if unknown.
     * @param values Signal values */
    void ingestMultipleSignalValuesByName( Timestamp timestamp,
                                           const std::vector<std::pair<std::string, DecodedSignalValue>> &values );

#ifdef FWE_FEATURE_EXTERNAL_GPS
    /**
     * @brief Sets the location for the ExternalGpsSource
     * @param latitude NMEA latitude in degrees
     * @param longitude NMEA longitude in degrees
     */
    void setExternalGpsLocation( double latitude, double longitude );
#endif

#ifdef FWE_FEATURE_AAOS_VHAL
    /**
     * Returns a vector of vehicle property info
     *
     * @return Vehicle property info, with each member containing an array with 4 values:
     * - Vehicle property ID
     * - Area index
     * - Result index
     * - Signal ID
     */
    std::vector<std::array<uint32_t, 4>> getVehiclePropertyInfo();

    /**
     * @brief Sets an Android Automotive vehicle property
     * @param signalId Signal ID
     * @param value Vehicle property value
     */
    void setVehicleProperty( uint32_t signalId, const DecodedSignalValue &value );
#endif

    /**
     * @brief Return a status summary, including the MQTT connection status, the campaign ARNs,
     *        and the number of payloads sent.
     * @return String with the status summary
     */
    std::string getStatusSummary();

private:
    // atomic state of the bus. If true, we should stop
    bool shouldStop() const;
    // Main work function for the thread
    static void doWork( void *data );

public:
    std::shared_ptr<DataSenderQueue> mCollectedDataReadyToPublish;
    // Object for handling persistency for decoder manifest, collectionSchemes and edge to cloud payload
    std::shared_ptr<CacheAndPersist> mPersistDecoderManifestCollectionSchemesAndData;

private:
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;

    Signal mWait;
    Timer mTimer;

    Timer mPrintMetricsCyclicTimer;
    uint64_t mPrintMetricsCyclicPeriodMs{ 0 }; // default to 0 which means no cyclic printing

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    CANInterfaceIDTranslator mCANIDTranslator;
    std::shared_ptr<OBDOverCANModule> mOBDOverCANModule;
    std::vector<std::unique_ptr<CANDataSource>> mCANDataSources;
    std::unique_ptr<ExternalCANDataSource> mExternalCANDataSource;
    std::unique_ptr<CANDataConsumer> mCANDataConsumer;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;

    std::unique_ptr<TopicConfig> mTopicConfig;
    std::shared_ptr<IConnectivityModule> mConnectivityModule;
    std::shared_ptr<ISender> mMqttSender;
    std::shared_ptr<IReceiver> mReceiverCollectionSchemeList;
    std::shared_ptr<IReceiver> mReceiverDecoderManifest;
    std::shared_ptr<PayloadManager> mPayloadManager;
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    bool mStoreAndForwardEnabled = true;
    std::shared_ptr<Aws::IoTFleetWise::Store::StreamManager> mStreamManager;
    std::shared_ptr<Aws::IoTFleetWise::Store::StreamForwarder> mStreamForwarder;
#endif
#ifdef FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES
    std::unique_ptr<CustomFunctionMultiRisingEdgeTrigger> mCustomFunctionMultiRisingEdgeTrigger;
#endif

    std::shared_ptr<Schema> mSchemaPtr;
    std::shared_ptr<CheckinSender> mCheckinSender;
    std::shared_ptr<CollectionSchemeManager> mCollectionSchemeManagerPtr;

    std::shared_ptr<RawData::BufferManager> mRawBufferManager;
    std::shared_ptr<CollectionInspectionEngine> mCollectionInspectionEngine;
    std::shared_ptr<CollectionInspectionWorkerThread> mCollectionInspectionWorkerThread;

    std::shared_ptr<DataSenderManager> mDataSenderManager;
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
    std::shared_ptr<ActuatorCommandManager> mActuatorCommandManager;
    std::unique_ptr<CommandSchema> mCommandSchema;
    std::shared_ptr<CanCommandDispatcher> mCanCommandDispatcher;
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
    std::shared_ptr<Aws::Auth::AWSCredentialsProvider> mAwsCredentialsProvider;
    std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> mTransferManagerExecutor;
    std::mutex mTransferManagerExecutorMutex;
    std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> getTransferManagerExecutor();
#endif
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::shared_ptr<S3Sender> mS3Sender;
#endif
#ifdef FWE_FEATURE_IWAVE_GPS
    std::shared_ptr<IWaveGpsSource> mIWaveGpsSource;
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
    std::shared_ptr<ExternalGpsSource> mExternalGpsSource;
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
    std::shared_ptr<AaosVhalSource> mAaosVhalSource;
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

} // namespace IoTFleetWise
} // namespace Aws
