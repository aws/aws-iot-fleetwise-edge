// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "CameraPubSubTypes.h"
#include "ClockHandler.h"
#include "IDDSPublisher.h"
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

using namespace Aws::IoTFleetWise::Platform::Linux;

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
    ~CameraDataPublisher() override;

    CameraDataPublisher( const CameraDataPublisher & ) = delete;
    CameraDataPublisher &operator=( const CameraDataPublisher & ) = delete;
    CameraDataPublisher( CameraDataPublisher && ) = delete;
    CameraDataPublisher &operator=( CameraDataPublisher && ) = delete;

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
    mutable std::mutex mThreadMutex;
    Timer mTimer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    Platform::Linux::Signal mWait;
    DomainParticipant *mDDSParticipant{ nullptr };
    Publisher *mDDSPublisher{ nullptr };
    Topic *mDDSTopic{ nullptr };
    DataWriter *mDDSWriter{ nullptr };
    TypeSupport mDDStype{ new CameraDataRequestPubSubType() };
    mutable std::mutex mRequesMutex;
    CameraDataRequest mRequest;
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws
