// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANDataConsumer.h"
#include "CANDataSource.h"
#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CollectionInspectionWorkerThread.h"
#include "CollectionSchemeManager.h"
#include "DataSenderManager.h"
#include "DataSenderManagerWorkerThread.h"
#include "ExternalCANDataSource.h"
#include "IConnectivityChannel.h"
#include "IConnectivityModule.h"
#include "OBDDataTypes.h"
#include "OBDOverCANModule.h"
#include "PayloadManager.h"
#include "RemoteProfiler.h"
#include "Schema.h"
#include "Signal.h"
#include "Thread.h"
#include "TimeTypes.h"
#include "Timer.h"
#include <atomic>
#include <cstdint>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    bool connect( const Json::Value &config );
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
    void setVehicleProperty( uint32_t signalId, double value );
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
    static constexpr uint64_t DEFAULT_RETRY_UPLOAD_PERSISTED_INTERVAL_MS = 10000;

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

    std::shared_ptr<IConnectivityModule> mConnectivityModule;
    std::shared_ptr<IConnectivityChannel> mConnectivityChannelSendVehicleData;
    std::shared_ptr<IConnectivityChannel> mConnectivityChannelSendCheckin;
    std::shared_ptr<IConnectivityChannel> mConnectivityChannelReceiveCollectionSchemeList;
    std::shared_ptr<IConnectivityChannel> mConnectivityChannelReceiveDecoderManifest;
    std::shared_ptr<PayloadManager> mPayloadManager;

    std::shared_ptr<Schema> mSchemaPtr;
    std::shared_ptr<CollectionSchemeManager> mCollectionSchemeManagerPtr;

    std::shared_ptr<CollectionInspectionWorkerThread> mCollectionInspectionWorkerThread;

    std::shared_ptr<DataSenderManager> mDataSenderManager;
    std::shared_ptr<DataSenderManagerWorkerThread> mDataSenderManagerWorkerThread;

    std::unique_ptr<RemoteProfiler> mRemoteProfiler;
    std::shared_ptr<IConnectivityChannel> mConnectivityChannelMetricsUpload;
    std::shared_ptr<IConnectivityChannel> mConnectivityChannelLogsUpload;

#ifdef FWE_FEATURE_IWAVE_GPS
    std::shared_ptr<IWaveGpsSource> mIWaveGpsSource;
#endif
#ifdef FWE_FEATURE_EXTERNAL_GPS
    std::shared_ptr<ExternalGpsSource> mExternalGpsSource;
#endif
#ifdef FWE_FEATURE_AAOS_VHAL
    std::shared_ptr<AaosVhalSource> mAaosVhalSource;
#endif
};

} // namespace IoTFleetWise
} // namespace Aws
