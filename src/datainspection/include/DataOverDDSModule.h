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
#include "ClockHandler.h"
#include "InspectionEventListener.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "dds/DDSDataTypes.h"
#include "dds/IDDSPublisher.h"
#include "dds/IDDSSubscriber.h"
#include "dds/SensorDataListener.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform;
using namespace Aws::IoTFleetWise::VehicleNetwork;

/**
 * @brief This module handles the collection of sensor data that is
 * distributed over the Data Distribution Service.
 * It manages the life cycle of the data readers and data writers in
 * the configured DDS domains.
 * This module owns a thread that intercepts event triggers from the
 * inspection engine and forwards them into the underlying protocol data readers.
 * It also intercepts notifications from the protocol data writer when data
 * has been received from the network and triggers the cloud offboardconnectivity
 * in order to upload it to IoTFleetWise's Data Plane.
 */
class DataOverDDSModule : public InspectionEventListener, public SensorDataListener
{
public:
    DataOverDDSModule();
    ~DataOverDDSModule();
    /**
     * @brief Initialize the module by creating the DDS readers and writers
     * accroding to the data source configuration.
     * @param ddsDataSourcesConfig Set of DDS Data sources configuration. Each source will have
     * one reader and one writer created.
     * @return True if successful. False if no source is provided or the sources
     * are wrongly configured.
     */
    bool init( const DDSDataSourcesConfig &ddsDataSourcesConfig );

    /**
     * @brief Connects the DDS readers/writers to their corresponding DDS Domains.
     * then  start monitoring the DDS Traffic.
     * @return True if successful. False otherwise.
     */
    bool connect();

    /**
     * @brief disconnects the DDS readers/writers from their corresponding DDS Domains.
     * @return True if successful. False otherwise.
     */
    bool disconnect();

    /**
     * @brief Returns the health state of the DDS Readers/Writers.
     * @return True if all readers/writers are healthy. False otherwise.
     */
    bool isAlive();

    /**
     * @brief Overwrite of InspectionEventListener notification. Upon a reception of this event,
     * the corresponding data writer of the sourceId is invoked to request the data.
     * @param eventMetadata The event metadata.
     */
    void onEventOfInterestDetected( const std::vector<EventMetadata> &eventMetadata ) override;

    /**
     * @brief Overwrite of SensorDataListener notification. Upon a reception of this event,
     * the corresponding metadata is processed.
     * @param artifactMetadata The artifact metadata.
     */
    void onSensorArtifactAvailable( const SensorArtifactMetadata &artifactMetadata ) override;

private:
    // Start the  worker thread
    bool start();
    // Stop the worker thread
    bool stop();
    // Intercepts stop signals
    bool shouldStop() const;

    /**
     * @brief Work function of the module.
     * The thread is typically waiting on a conditional variable and wakes up upon :
     * 1- The inspection Engine raises an event of interest, and so rich sensor data is requested
     * from the DDS network.
     * 2- A DDS node fetches an artifact from the network ( e.g. Camera snapshot ) and shares the
     * artifact metadata with the module. In this case, the artifact is processed e.g. shared with
     * the cloud.
     */
    static void doWork( void *data );

    Thread mThread;
    std::atomic<bool> mShouldStop;
    std::atomic<bool> mNewEventReceived{ false };
    mutable std::recursive_mutex mThreadMutex;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    Platform::Signal mWait;
    Timer mTimer;
    // For Subscriber, we don't need to track the sourceID.
    std::vector<DDSSubscriberPtr> mSubscribers;
    // For Publisher, we need to track the sourceID as we need to know which Publisher we
    // need to invoke upon a new event.
    std::map<uint32_t, DDSPublisherPtr> mPublishers;
    mutable std::mutex mEventMetaMutex;
    // To protect against race condition during find and emplace ops on the
    // Pub/Sub containers.
    mutable std::mutex mPubSubMutex;
    // One item for each source we want to request e.g. multiple cameras.
    std::vector<EventMetadata> mEventMetatdata;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
