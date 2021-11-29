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
#include "INetworkChannelConsumer.h"
#include "LoggingModule.h"
#include "NetworkChannelBinder.h"
#include "OBDOverCANModule.h"
#include "RemoteProfiler.h"
#include "Schema.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "businterfaces/INetworkChannelBridge.h"
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
using namespace Aws::IoTFleetWise::Platform;
using namespace Aws::IoTFleetWise::Platform::PersistencyManagement;
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
    static const uint32_t MAX_NUMBER_OF_SIGNAL_TO_TRACE_LOG = 6;
    IoTFleetWiseEngine();
    virtual ~IoTFleetWiseEngine();
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
     *
     */
    void checkAndSendRetrievedData();

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
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::recursive_mutex mThreadMutex;

    Platform::Signal mWait;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::unique_ptr<NetworkChannelBinder> mBinder;
    CollectionSchemePtr mCollectionScheme;

    std::shared_ptr<OBDOverCANModule> mOBDOverCANModule;
    std::shared_ptr<DataCollectionSender> mDataCollectionSender;

    EventTriggers mSignalEventTriggers;
    EventTrigger mTimerBasedTrigger;

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
};
} // namespace ExecutionManagement
} // namespace IoTFleetWise
} // namespace Aws
