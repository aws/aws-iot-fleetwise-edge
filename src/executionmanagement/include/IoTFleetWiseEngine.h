// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "AwsIotChannel.h"
#include "AwsIotConnectivityModule.h"
#include "CANDataSource.h"
#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "ClockHandler.h"
#include "CollectionInspectionWorkerThread.h"
#include "CollectionSchemeManager.h"
#include "DataCollectionSender.h"
#ifdef FWE_FEATURE_CAMERA
#include "DataOverDDSModule.h"
#endif // FWE_FEATURE_CAMERA
#ifdef FWE_FEATURE_IWAVE_GPS
#include "IWaveGpsSource.h"
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
#include "ExternalGpsSource.h"
#endif
#include "ExternalCANDataSource.h"
#include "IDataReadyToPublishListener.h"
#include "OBDOverCANModule.h"
#include "RemoteProfiler.h"
#include "Schema.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
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
    void ingestExternalCANMessage( const std::string &interfaceId,
                                   Timestamp timestamp,
                                   uint32_t messageId,
                                   const std::vector<uint8_t> &data );

#ifdef FWE_FEATURE_EXTERNAL_GPS
    /**
     * @brief Sets the location for the ExternalGpsSource
     * @param latitude NMEA latitude in degrees
     * @param longitude NMEA longitude in degrees
     */
    void setExternalGpsLocation( double latitude, double longitude );
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

    Timer mPrintMetricsCyclicTimer;
    uint64_t mPrintMetricsCyclicPeriodMs{ 0 }; // default to 0 which means no cyclic printing

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    CANInterfaceIDTranslator mCANIDTranslator;
    std::shared_ptr<OBDOverCANModule> mOBDOverCANModule;
    std::vector<std::unique_ptr<CANDataSource>> mCANDataSources;
    std::unique_ptr<ExternalCANDataSource> mExternalCANDataSource;
    std::unique_ptr<CANDataConsumer> mCANDataConsumer;
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
#ifdef FWE_FEATURE_CAMERA
    // DDS Module
    std::shared_ptr<DataOverDDSModule> mDataOverDDSModule;
#endif // FWE_FEATURE_CAMERA
#ifdef FWE_FEATURE_IWAVE_GPS
    std::shared_ptr<IWaveGpsSource> mIWaveGpsSource;
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
    std::shared_ptr<ExternalGpsSource> mExternalGpsSource;
#endif
};
} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
