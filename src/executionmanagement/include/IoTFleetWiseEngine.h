// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "AwsIotChannel.h"
#include "AwsIotConnectivityModule.h"
#include "CacheAndPersist.h"
#include "ClockHandler.h"
#include "CollectionInspectionWorkerThread.h"
#include "CollectionScheme.h"
#include "CollectionSchemeListener.h"
#include "CollectionSchemeManager.h"
#include "DataCollectionSender.h"
#ifdef FWE_FEATURE_CAMERA
#include "DataOverDDSModule.h"
#endif // FWE_FEATURE_CAMERA
#include "IDataReadyToPublishListener.h"
#include "LoggingModule.h"
#include "OBDOverCANModule.h"
#include "RemoteProfiler.h"
#include "Schema.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "VehicleDataSourceBinder.h"
#include "businterfaces/AbstractVehicleDataSource.h"
#include <atomic>
#include <json/json.h>
#include <map>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{
namespace ExecutionManagement
{
using namespace Aws::IoTFleetWise::DataManagement;
using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::Platform::Linux::PersistencyManagement;
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;

/**
 * @brief Main AWS IoT FleetWise Bootstrap module.
 * 1- Initializes the Connectivity Module
 * 2- Initializes the Inspection Engine
 * 3- Initializes the CollectionScheme Ingestion & Management modules
 * 5- Initializes the Vehicle Network module.
 * The bootstrap executes a worker thread that listens to the Inspection
 * Engine notification, upon which the offboardconnectivity module is invoked to
 * send the data to the cloud.
 */
class IoTFleetWiseEngine : public IDataReadyToPublishListener
{
public:
    static const uint32_t MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG;
    static const uint64_t FAST_RETRY_UPLOAD_PERSISTED_INTERVAL_MS;    // retry every second
    static const uint64_t DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS; // retry every 10 second

    IoTFleetWiseEngine();
    ~IoTFleetWiseEngine() override;

    IoTFleetWiseEngine( const IoTFleetWiseEngine & ) = delete;
    IoTFleetWiseEngine &operator=( const IoTFleetWiseEngine & ) = delete;
    IoTFleetWiseEngine( IoTFleetWiseEngine && ) = delete;
    IoTFleetWiseEngine &operator=( IoTFleetWiseEngine && ) = delete;

    bool connect( const Json::Value &config );
    bool start();
    bool stop();
    bool disconnect();
    bool isAlive();

    /**
     * @brief Callback from the Inspection Engine to wake up this thread and
     * publish the data to the cloud.
     */
    void onDataReadyToPublish() override;

    /**
     * @brief Check if the data was persisted in the last cycle due to no offboardconnectivity,
     *        retrieve all the data and send
     * @return true if either no data persisted or all persisted data was handed over to connectivity
     */
    bool checkAndSendRetrievedData();

    /**
     * @brief Attach a vehicle data source to IoTFleetWise. All functions in the data source must be
     * thread safe. The source instance is needed so that FleetWise can access its output data
     * buffer ( holding the vehicle data messages ) and to register to the connect and disconnect
     * events. A vehicle data consumer will be attached to the source.
     * This function can be called either on startup or runtime.
     * The life cycle of the source is managed by the caller ( e.g. init, connect, disconnect).
     */
    void attachVehicleDataSource( VehicleDataSourcePtr vehicleDataSource );

    /**
     * @brief Detach a vehicle data source to IoTFleetWise. No further data produced by this source
     * will be visible to IoTFleetWise.
     * This function can be called either on shutdown or runtime.
     * The life cycle of the source is managed by the caller ( e.g. init, connect, disconnect).
     */
    static void detachVehicleDataSource( VehicleDataSourcePtr vehicleDataSource );

private:
    // atomic state of the bus. If true, we should stop
    bool shouldStop() const;
    // Main work function for the thread
    static void doWork( void *data );

public:
    std::shared_ptr<CollectedDataReadyToPublish> mCollectedDataReadyToPublish;
    // Object for handling persistency for decoder manifest, collectionSchemes and edge to cloud payload
    std::shared_ptr<CacheAndPersist> mPersistDecoderManifestCollectionSchemesAndData;

private:
    static constexpr uint64_t DEFAULT_PERSISTENCY_UPLOAD_RETRY_INTERVAL_MS = 0;
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;

    Platform::Linux::Signal mWait;
    Timer mTimer;
    Timer mRetrySendingPersistedDataTimer;
    uint64_t mPersistencyUploadRetryIntervalMs{ DEFAULT_PERSISTENCY_UPLOAD_RETRY_INTERVAL_MS };

    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::unique_ptr<VehicleDataSourceBinder> mVehicleDataSourceBinder;
    CollectionSchemePtr mCollectionScheme;

    std::shared_ptr<OBDOverCANModule> mOBDOverCANModule;
    std::shared_ptr<DataCollectionSender> mDataCollectionSender;

    std::shared_ptr<AwsIotConnectivityModule> mAwsIotModule;
    std::shared_ptr<AwsIotChannel> mAwsIotChannelSendCanData;
    std::shared_ptr<AwsIotChannel> mAwsIotChannelSendCheckin;
    std::shared_ptr<AwsIotChannel> mAwsIotChannelReceiveCollectionSchemeList;
    std::shared_ptr<AwsIotChannel> mAwsIotChannelReceiveDecoderManifest;
    std::shared_ptr<PayloadManager> mPayloadManager;

    std::shared_ptr<Schema> mSchemaPtr;
    std::shared_ptr<CollectionSchemeManager> mCollectionSchemeManagerPtr;

    std::shared_ptr<CollectionInspectionWorkerThread> mCollectionInspectionWorkerThread;

    std::unique_ptr<RemoteProfiler> mRemoteProfiler;
    std::shared_ptr<AwsIotChannel> mAwsIotChannelMetricsUpload;
    std::shared_ptr<AwsIotChannel> mAwsIotChannelLogsUpload;
    VehicleDataSourcePtr mVehicleDataSource;
#ifdef FWE_FEATURE_CAMERA
    // DDS Module
    std::shared_ptr<DataOverDDSModule> mDataOverDDSModule;
#endif // FWE_FEATURE_CAMERA
};
} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
