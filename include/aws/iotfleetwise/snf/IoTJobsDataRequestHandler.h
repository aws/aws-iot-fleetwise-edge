// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include "aws/iotfleetwise/snf/StreamManager.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This class handles the receipt of Data Upload Request Jobs from the Cloud.
 */
class IoTJobsDataRequestHandler
{
public:
    /**
     * @brief Construct the IoTJobsDataRequestHandler which can receive Jobs from the Cloud.
     * @param mqttSender Sender to send any IoT Job requests
     * @param receiverIotJobs Receiver to receive IoT Jobs
     * @param receiverAcceptedJobDocuments Receiver to receive accepted IoT Job documents
     * @param receiverRejectedJobDocuments Receiver to receive rejected IoT Job document requests
     * @param receiverAcceptedPendingJobs Receiver to receive accepted pending IoT Jobs
     * @param receiverRejectedPendingJobs Receiver to receive rejected pending IoT Job requests
     * @param receiverAcceptedUpdateJobs Receiver to receive accepted IoT Job execution updates
     * @param receiverRejectedUpdateJobs Receiver to receive rejected IoT Job execution updates
     * @param receiverCanceledJobs Receiver to receive IoT Jobs that were canceled in the cloud
     * @param streamManager Contains information about active collection schemes
     * @param streamForwarder Contains functions to upload data
     * @param thingName ThingName
     */
    IoTJobsDataRequestHandler( ISender &mqttSender,
                               IReceiver &receiverIotJobs,
                               IReceiver &receiverAcceptedJobDocuments,
                               IReceiver &receiverRejectedJobDocuments,
                               IReceiver &receiverAcceptedPendingJobs,
                               IReceiver &receiverRejectedPendingJobs,
                               IReceiver &receiverAcceptedUpdateJobs,
                               IReceiver &receiverRejectedUpdateJobs,
                               IReceiver &receiverCanceledJobs,
                               Aws::IoTFleetWise::Store::StreamManager &streamManager,
                               Aws::IoTFleetWise::Store::StreamForwarder &streamForwarder,
                               std::string thingName );

    static uint64_t convertEndTimeToMS( const std::string &iso8601 );

    /**
     * @brief Callback to be called when the connection is successful and all senders and receivers are ready
     */
    void onConnectionEstablished();

private:
    void onIotJobReceived( const uint8_t *buf, size_t size );
    void onIotJobDocumentAccepted( const uint8_t *buf, size_t size );
    static void onIotJobDocumentRejected( const uint8_t *buf, size_t size );
    void onPendingJobsAccepted( const uint8_t *buf, size_t size );
    void onPendingJobsRejected( const uint8_t *buf, size_t size );
    void onUpdateJobStatusAccepted( const uint8_t *buf, size_t size );
    static void onUpdateJobStatusRejected( const uint8_t *buf, size_t size );
    void onCanceledJobReceived( const uint8_t *buf, size_t size );
    void onJobUploadCompleted( Store::CampaignID campaignId );

    static std::string random_string( size_t length );

    void sendGetPendingExecutions();

    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
    constexpr static size_t RANDOM_STRING_SIZE = 10;

    enum struct jobStatus
    {
        QUEUED,
        IN_PROGRESS,
        FAILED,
        SUCCEEDED,
        CANCELED,
        TIMED_OUT,
        REJECTED,
        REMOVED,
    };

    std::string mThingName;
    ISender &mMqttSender;
    Aws::IoTFleetWise::Store::StreamManager &mStreamManager;
    Aws::IoTFleetWise::Store::StreamForwarder &mStreamForwarder;
    std::map<std::string, jobStatus> mJobToStatus;
    std::map<std::string, std::string> mJobToCampaignId;

    std::mutex mCampaignMutex;
};

} // namespace IoTFleetWise
} // namespace Aws
