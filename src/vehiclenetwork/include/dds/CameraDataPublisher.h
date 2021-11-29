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
#include "CameraPubSubTypes.h"
#include "ClockHandler.h"
#include "IDDSPublisher.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

using namespace Aws::IoTFleetWise::Platform;

/**
 * @brief IDDSPublisher implementation for the Camera Sensor.
 * This instance receives a request to retrieve Camera data for a given event
 * for the Inspection layer, transforms that into a DDS Message and publishes it to the
 * corresponding topic.
 */
class CameraDataPublisher : public IDDSPublisher
{
public:
    CameraDataPublisher();
    ~CameraDataPublisher();

    bool init( const DDSDataSourceConfig &dataSourceConfig ) override;

    bool connect() override;

    bool disconnect() override;

    bool isAlive() override;

    void publishDataRequest( const DDSDataRequest &dataRequest ) override;

    /**
     * @brief From DataWriterListener.
     * method to be called when the Publisher is matched with a subscriber at the DDS Endpoint
     * @param writer DataWriter, not used.
     * @param info The publication matched status. The attribute we focus on current_count_change
     */
    void on_publication_matched( DataWriter *writer, const PublicationMatchedStatus &info ) override;

private:
    // Start the bus thread
    bool start();
    // Stop the bus thread
    bool stop();
    // atomic state of the bus. If true, we should stop
    bool shouldStop() const;
    /**
     * @brief Main work function.
     * Typically this function waits on conditional variable until it's set.
     * The conditional variable gets set when on_data_available is called by the DDS Stack.
     * After that, in the current phase of implementation,
     *  we simply raise onSensorArtifactAvailable.
     *  Next step is to persist the data into a physical location.
     * @param data data pointer from the stack.
     */
    static void doWork( void *data );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mIsAlive{ false };
    std::atomic<bool> mRequestCompleted{ true };
    mutable std::recursive_mutex mThreadMutex;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    Platform::Signal mWait;
    DomainParticipant *mDDSParticipant;
    Publisher *mDDSPublisher;
    Topic *mDDSTopic;
    DataWriter *mDDSWriter;
    TypeSupport mDDStype;
    mutable std::mutex mRequesMutex;
    CameraDataRequest mRequest;
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
