// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/IoTJobsDataRequestHandler.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include <boost/date_time/gregorian/greg_date.hpp>
#include <boost/date_time/gregorian/greg_day.hpp>
#include <boost/date_time/gregorian/greg_month.hpp>
#include <boost/date_time/gregorian/greg_year.hpp>
#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/date_time/posix_time/posix_time_config.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/time.hpp>
#include <ctime>
#include <iomanip>
#include <istream>
#include <json/json.h>
#include <random>
#include <set>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

IoTJobsDataRequestHandler::IoTJobsDataRequestHandler( ISender &mqttSender,
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
                                                      std::string thingName )
    : mThingName( std::move( thingName ) )
    , mMqttSender( mqttSender )
    , mStreamManager( streamManager )
    , mStreamForwarder( streamForwarder )
{
    mStreamForwarder.registerJobCompletionCallback( [this]( const Store::CampaignID &campaignId ) {
        onJobUploadCompleted( campaignId );
    } );

    // register the listeners
    receiverIotJobs.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onIotJobReceived( receivedMessage.buf, receivedMessage.size );
    } );
    receiverAcceptedJobDocuments.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onIotJobDocumentAccepted( receivedMessage.buf, receivedMessage.size );
    } );
    receiverRejectedJobDocuments.subscribeToDataReceived( []( const ReceivedConnectivityMessage &receivedMessage ) {
        onIotJobDocumentRejected( receivedMessage.buf, receivedMessage.size );
    } );
    receiverAcceptedPendingJobs.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onPendingJobsAccepted( receivedMessage.buf, receivedMessage.size );
    } );
    receiverRejectedPendingJobs.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onPendingJobsRejected( receivedMessage.buf, receivedMessage.size );
    } );
    receiverAcceptedUpdateJobs.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onUpdateJobStatusAccepted( receivedMessage.buf, receivedMessage.size );
    } );
    receiverRejectedUpdateJobs.subscribeToDataReceived( []( const ReceivedConnectivityMessage &receivedMessage ) {
        onUpdateJobStatusRejected( receivedMessage.buf, receivedMessage.size );
    } );
    receiverCanceledJobs.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onCanceledJobReceived( receivedMessage.buf, receivedMessage.size );
    } );
}

void
IoTJobsDataRequestHandler::onConnectionEstablished()
{

    // wait until connected to check to see if any jobs were QUEUED or IN_PROGRESS
    sendGetPendingExecutions();
}

void
IoTJobsDataRequestHandler::onPendingJobsAccepted( const uint8_t *buf, size_t size )
{
    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    Json::Reader reader;
    Json::Value pendingJobs;

    if ( reader.parse( std::string( buf, buf + size ), pendingJobs ) )
    {
        auto inProgressJobs = pendingJobs["inProgressJobs"];

        if ( !inProgressJobs.empty() )
        {
            // Jobs were IN_PROGRESS when FWE restarted. Need to start data upload again
            if ( !inProgressJobs.isArray() )
            {
                FWE_LOG_ERROR( "Expected inProgressJobs Jobs to be a list in GetPendingJobExecutions response" );
                return;
            }

            for ( Json::Value::iterator it = inProgressJobs.begin(); it != inProgressJobs.end(); it++ )
            {
                Json::Value job = *it;

                if ( job["jobId"].empty() )
                {
                    FWE_LOG_ERROR( "No jobId in the pending job execution summary" );
                    continue;
                }

                if ( !job["jobId"].isString() )
                {
                    FWE_LOG_ERROR( "The jobId received is not a string" );
                    continue;
                }

                std::string jobId = job["jobId"].asString();

                if ( jobId.empty() )
                {
                    // Note: Need this empty check since Json::Value [] operator will
                    // "Access an object value by name, create a null member if it does not exist."
                    FWE_LOG_ERROR( "The jobId received is an empty string" );
                    continue;
                }

                bool isJobUploading = false;
                {
                    std::lock_guard<std::mutex> lock( mCampaignMutex );
                    isJobUploading = mJobToStatus.find( jobId ) != mJobToStatus.end() &&
                                     ( mJobToStatus[jobId] == jobStatus::IN_PROGRESS );
                }
                if ( isJobUploading )
                {
                    // Since this jobId is already uploading data, there is no need to request the job doc
                    FWE_LOG_TRACE( jobId + " is already uploading data" );
                    continue;
                }

                Json::StreamWriterBuilder builder;
                Json::Value requestJobDoc;
                requestJobDoc["jobId"] = jobId;
                requestJobDoc["thingName"] = mThingName;
                requestJobDoc["includeJobDocument"] = true;

                const std::string data = Json::writeString( builder, requestJobDoc );
                // Publishing is async and non-blocking
                std::string topic = mMqttSender.getTopicConfig().getJobExecutionTopic( jobId );
                mMqttSender.sendBuffer( topic,
                                        reinterpret_cast<const uint8_t *>( data.c_str() ),
                                        data.size(),
                                        [topic]( ConnectivityError result ) {
                                            if ( result == ConnectivityError::Success )
                                            {
                                                FWE_LOG_TRACE( "Successfully sent message on topic " + topic );
                                            }
                                            else
                                            {
                                                FWE_LOG_ERROR( "Send error " +
                                                               std::to_string( static_cast<uint32_t>( result ) ) );
                                            }
                                        } );
            }
        }

        auto queuedJobs = pendingJobs["queuedJobs"];

        if ( !queuedJobs.empty() )
        {
            // Jobs were QUEUED when FWE reconnected or restarted
            // The onIotJobReceived handler won't receive a message unless a "job is added to or removed from the list
            // of pending job executions for a thing or the first job execution in the list changes"
            // Thus, we need to process the queued jobs

            if ( !queuedJobs.isArray() )
            {
                FWE_LOG_ERROR( "Expected queuedJobs Jobs to be a list in GetPendingJobExecutions response" );
                return;
            }

            for ( Json::Value::iterator it = queuedJobs.begin(); it != queuedJobs.end(); it++ )
            {
                Json::Value job = *it;

                if ( job["jobId"].empty() )
                {
                    FWE_LOG_ERROR( "No jobId in the pending job execution summary" );
                    continue;
                }

                if ( !job["jobId"].isString() )
                {
                    FWE_LOG_ERROR( "The jobId received is not a string" );
                    continue;
                }

                std::string jobId = job["jobId"].asString();

                if ( jobId.empty() )
                {
                    // This should never happen since IoT Jobs should never send us "" as a jobId
                    FWE_LOG_ERROR( "The jobId received is an empty string" );
                    continue;
                }

                Json::StreamWriterBuilder builder;
                Json::Value requestJobDoc;
                requestJobDoc["jobId"] = jobId;
                requestJobDoc["thingName"] = mThingName;
                requestJobDoc["includeJobDocument"] = true;

                const std::string data = Json::writeString( builder, requestJobDoc );
                // Publishing is async and non-blocking
                std::string topic = mMqttSender.getTopicConfig().getJobExecutionTopic( jobId );
                mMqttSender.sendBuffer( topic,
                                        reinterpret_cast<const uint8_t *>( data.c_str() ),
                                        data.size(),
                                        [topic]( ConnectivityError result ) {
                                            if ( result == ConnectivityError::Success )
                                            {
                                                FWE_LOG_TRACE( "Successfully sent message on topic " + topic );
                                            }
                                            else
                                            {
                                                FWE_LOG_ERROR( "Send error " +
                                                               std::to_string( static_cast<uint32_t>( result ) ) );
                                            }
                                        } );
            }
        }
    }
}

void
IoTJobsDataRequestHandler::onPendingJobsRejected( const uint8_t *buf, size_t size )
{
    FWE_LOG_ERROR( "GetPendingJobExecutions request was rejected" );

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    FWE_LOG_INFO( "Retrying GetPendingJobExecutions request" );
    sendGetPendingExecutions();
}

void
IoTJobsDataRequestHandler::onIotJobReceived( const uint8_t *buf, size_t size )
{
    // Check for an empty input data
    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    Json::Reader reader;
    Json::Value newJobs;
    if ( reader.parse( std::string( buf, buf + size ), newJobs ) )
    {
        if ( newJobs["jobs"].empty() )
        {
            FWE_LOG_TRACE( "No Jobs received in Store and Forward data upload request" );
            return;
        }

        auto listJobs = newJobs["jobs"]["QUEUED"];

        if ( listJobs.empty() )
        {
            FWE_LOG_ERROR( "No QUEUED Jobs received in Store and Forward data upload request" );
            return;
        }

        FWE_LOG_INFO( "Received Store and Forward data upload request" );

        if ( !listJobs.isArray() )
        {
            FWE_LOG_ERROR( "Expected QUEUED Jobs to be a list in the Store and Forward data upload request" );
            return;
        }

        for ( Json::Value::iterator it = listJobs.begin(); it != listJobs.end(); it++ )
        {
            Json::Value job = *it;

            if ( job["jobId"].empty() )
            {
                FWE_LOG_ERROR( "No jobId received in Store and Forward data upload request" );
                continue;
            }

            if ( !job["jobId"].isString() )
            {
                FWE_LOG_ERROR( "The jobId received is not a string" );
                continue;
            }

            std::string jobId = job["jobId"].asString();

            if ( jobId.empty() )
            {
                // This should never happen since IoT Jobs should never send us "" as a jobId
                FWE_LOG_ERROR( "The jobId received is an empty string" );
                continue;
            }

            Json::StreamWriterBuilder builder;
            Json::Value requestJobDoc;
            requestJobDoc["jobId"] = jobId;
            requestJobDoc["thingName"] = mThingName;
            requestJobDoc["includeJobDocument"] = true;

            const std::string data = Json::writeString( builder, requestJobDoc );
            // Publishing is async and non-blocking
            std::string topic = mMqttSender.getTopicConfig().getJobExecutionTopic( jobId );
            mMqttSender.sendBuffer( topic,
                                    reinterpret_cast<const uint8_t *>( data.c_str() ),
                                    data.size(),
                                    [topic]( ConnectivityError result ) {
                                        if ( result == ConnectivityError::Success )
                                        {
                                            FWE_LOG_TRACE( "Successfully sent message on topic " + topic );
                                        }
                                        else
                                        {
                                            FWE_LOG_ERROR( "Send error " +
                                                           std::to_string( static_cast<uint32_t>( result ) ) );
                                        }
                                    } );
        }
    }
    else
    {
        FWE_LOG_ERROR( "Received Store and Forward data upload request, but unable to parse received Job(s)" );
    }
}

void
IoTJobsDataRequestHandler::onIotJobDocumentAccepted( const uint8_t *buf, size_t size )
{
    // Check for an empty input data
    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    FWE_LOG_INFO( "Received Store and Forward data upload request document" );

    Json::Reader reader;
    Json::Value jobDocument;
    if ( reader.parse( std::string( buf, buf + size ), jobDocument ) )
    {
        std::string campaignArn;
        uint64_t endTime = 0;
        std::string jobId;

        auto execution = jobDocument["execution"];

        if ( execution.empty() )
        {
            FWE_LOG_ERROR( "Null or empty execution field in received upload request document" );
            return;
        }

        if ( execution["jobId"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty jobId in received upload request document" );
            return;
        }

        if ( !execution["jobId"].isString() )
        {
            FWE_LOG_ERROR( "The jobId in the upload request document is not a string" );
            return;
        }

        jobId = execution["jobId"].asString();

        if ( jobId.empty() )
        {
            // This should never happen since IoT Jobs should never send us "" as a jobId
            FWE_LOG_ERROR( "The jobId received in the upload request document is an empty string" );
            return;
        }

        if ( execution["jobDocument"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty jobDocument field in received upload request document" );
            return;
        }

        auto jobDoc = execution["jobDocument"];

        if ( jobDoc["parameters"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty parameters field in received job document" );
            return;
        }

        if ( jobDoc["parameters"]["campaignArn"].empty() || ( !jobDoc["parameters"]["campaignArn"].isString() ) )
        {
            FWE_LOG_ERROR( "Invalid campaignArn value in received job document" );
            return;
        }

        campaignArn = jobDoc["parameters"]["campaignArn"].asString();

        if ( ( !jobDoc["parameters"]["endTime"].empty() ) && jobDoc["parameters"]["endTime"].isString() )
        {
            // Specifying an endTime in the Job Document is optional
            endTime = convertEndTimeToMS( jobDocument["execution"]["jobDocument"]["parameters"]["endTime"].asString() );
        }

        Json::StreamWriterBuilder builder;
        Json::Value updateJobExecution;
        updateJobExecution["status"] = "IN_PROGRESS";
        // setting clientToken as the jobId so that we can access the jobId in the rejected topic
        updateJobExecution["clientToken"] = jobId;

        if ( execution["status"] == "IN_PROGRESS" )
        {
            // Job is already IN_PROGRESS meaning data upload was once happening
            // Restart data upload if campaignArn is still valid
            bool isJobUploading = false;
            {
                std::lock_guard<std::mutex> lock( mCampaignMutex );
                isJobUploading = mJobToStatus.find( jobId ) != mJobToStatus.end() &&
                                 ( mJobToStatus[jobId] == jobStatus::IN_PROGRESS );
            }
            if ( isJobUploading )
            {
                FWE_LOG_TRACE( jobId + " is already uploading data" )
                return;
            }

            if ( !mStreamManager.hasCampaign( campaignArn ) )
            {
                FWE_LOG_ERROR( "CampaignArn value " + campaignArn +
                               " in the received job document does not match the arn of a "
                               "store-and-forward campaign" );
                updateJobExecution["status"] = "REJECTED";

                {
                    std::lock_guard<std::mutex> lock( mCampaignMutex );
                    mJobToStatus[jobId] = jobStatus::REJECTED;
                }

                // Need to update job since something went wrong and we no longer have the specified s&f campaign
                const std::string data = Json::writeString( builder, updateJobExecution );
                // Publishing is async and non-blocking
                std::string topic = mMqttSender.getTopicConfig().updateJobExecutionTopic( jobId );
                mMqttSender.sendBuffer( topic,
                                        reinterpret_cast<const uint8_t *>( data.c_str() ),
                                        data.size(),
                                        [topic]( ConnectivityError result ) {
                                            if ( result == ConnectivityError::Success )
                                            {
                                                FWE_LOG_TRACE( "Successfully sent message on topic " + topic );
                                            }
                                            else
                                            {
                                                FWE_LOG_ERROR( "Send error " +
                                                               std::to_string( static_cast<uint32_t>( result ) ) );
                                            }
                                        } );
            }
            else
            {
                mStreamForwarder.beginJobForward( campaignArn, endTime );
                std::lock_guard<std::mutex> lock( mCampaignMutex );
                mJobToStatus[jobId] = jobStatus::IN_PROGRESS;
                mJobToCampaignId[jobId] = std::move( campaignArn );
            }
        }
        else
        {
            // Job is NOT already IN_PROGRESS
            if ( !mStreamManager.hasCampaign( campaignArn ) )
            {
                FWE_LOG_ERROR( "CampaignArn value " + campaignArn +
                               " in the received job document does not match the arn of a "
                               "store-and-forward campaign" );
                updateJobExecution["status"] = "REJECTED";
                std::lock_guard<std::mutex> lock( mCampaignMutex );
                mJobToStatus[jobId] = jobStatus::REJECTED;
            }
            else
            {
                mStreamForwarder.beginJobForward( campaignArn, endTime );
                std::lock_guard<std::mutex> lock( mCampaignMutex );
                mJobToStatus[jobId] = jobStatus::IN_PROGRESS;
                mJobToCampaignId[jobId] = std::move( campaignArn );
            }

            const std::string data = Json::writeString( builder, updateJobExecution );
            // Publishing is async and non-blocking
            std::string topic = mMqttSender.getTopicConfig().updateJobExecutionTopic( jobId );
            mMqttSender.sendBuffer( topic,
                                    reinterpret_cast<const uint8_t *>( data.c_str() ),
                                    data.size(),
                                    [topic]( ConnectivityError result ) {
                                        if ( result == ConnectivityError::Success )
                                        {
                                            FWE_LOG_TRACE( "Successfully sent message on topic " + topic );
                                        }
                                        else
                                        {
                                            FWE_LOG_ERROR( "Send error " +
                                                           std::to_string( static_cast<uint32_t>( result ) ) );
                                        }
                                    } );
        }
    }
    else
    {
        FWE_LOG_ERROR( "Unable to parse received Store and Forward data upload request document" );
    }
}

void
IoTJobsDataRequestHandler::onIotJobDocumentRejected( const uint8_t *buf, size_t size )
{
    FWE_LOG_ERROR( "DescribeJobExecution request was rejected" );

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    Json::Reader reader;
    Json::Value rejectedJobDocument;
    if ( reader.parse( std::string( buf, buf + size ), rejectedJobDocument ) )
    {
        std::string jobId;

        auto execution = rejectedJobDocument["execution"];

        if ( execution.empty() )
        {
            FWE_LOG_ERROR( "Null or empty execution field in received upload request document" );
            FWE_LOG_INFO( "Not retrying DescribeJobExecution request" );
            return;
        }

        if ( execution["jobId"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty jobId in received upload request document" );
            FWE_LOG_INFO( "Not retrying DescribeJobExecution request" );
            return;
        }

        if ( !execution["jobId"].isString() )
        {
            FWE_LOG_ERROR( "The jobId in the upload request document is not a string" );
            FWE_LOG_INFO( "Not retrying DescribeJobExecution request" );
            return;
        }

        jobId = execution["jobId"].asString();

        if ( jobId.empty() )
        {
            // This should never happen since IoT Jobs should never send us "" as a jobId
            FWE_LOG_ERROR( "The jobId received is an empty string" );
        }

        FWE_LOG_INFO( "Not retrying DescribeJobExecution request for jobId:  " + jobId );
    }
    else
    {
        FWE_LOG_INFO( "Not retrying DescribeJobExecution request" );
    }
}

void
IoTJobsDataRequestHandler::onUpdateJobStatusAccepted( const uint8_t *buf, size_t size )
{
    FWE_LOG_TRACE( "UpdateJobExecution request was accepted" );

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    Json::Reader reader;
    Json::Value acceptedUpdateJob;

    if ( reader.parse( std::string( buf, buf + size ), acceptedUpdateJob ) )
    {

        if ( acceptedUpdateJob["clientToken"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty clientToken in the accepted update request message" );
            return;
        }

        if ( !acceptedUpdateJob["clientToken"].isString() )
        {
            FWE_LOG_ERROR( "The clientToken in the accepted upload request message is not a string" );
            return;
        }

        std::string jobId = acceptedUpdateJob["clientToken"].asString();

        if ( jobId.empty() )
        {
            // checking that the jobId is not ""
            FWE_LOG_ERROR( "The jobId in the accepted upload request message is empty" );
            return;
        }

        std::lock_guard<std::mutex> lock( mCampaignMutex );
        if ( mJobToStatus.find( jobId ) != mJobToStatus.end() &&
             ( ( mJobToStatus[jobId] == jobStatus::SUCCEEDED ) || ( mJobToStatus[jobId] == jobStatus::REJECTED ) ) )
        {
            // if the job has reached a terminal state, then remove it from mJobToStatus
            mJobToStatus.erase( jobId );
        }
    }
}

void
IoTJobsDataRequestHandler::onUpdateJobStatusRejected( const uint8_t *buf, size_t size )
{
    FWE_LOG_ERROR( "UpdateJobExecution request was rejected" );

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    Json::Reader reader;
    Json::Value rejectedUpdateJob;

    if ( reader.parse( std::string( buf, buf + size ), rejectedUpdateJob ) )
    {

        if ( rejectedUpdateJob["clientToken"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty clientToken in the rejected update request message" );
            FWE_LOG_INFO( "Not retrying UpdateJobExecution request" );
            return;
        }

        if ( !rejectedUpdateJob["clientToken"].isString() )
        {
            FWE_LOG_ERROR( "The clientToken in the rejected upload request message is not a string" );
            FWE_LOG_INFO( "Not retrying UpdateJobExecution request" );
            return;
        }

        std::string jobId = rejectedUpdateJob["clientToken"].asString();

        if ( jobId.empty() )
        {
            // checking that the jobId is not ""
            FWE_LOG_ERROR( "The jobId in the rejected upload request message is empty" );
        }

        FWE_LOG_INFO( "Not retrying UpdateJobExecution request for JobId: " + jobId );
    }
    else
    {
        FWE_LOG_INFO( "Not retrying UpdateJobExecution request" );
    }
}

void
IoTJobsDataRequestHandler::onCanceledJobReceived( const uint8_t *buf, size_t size )
{
    FWE_LOG_INFO( "Received a canceled job notification" );

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty IoT Job data from Cloud" );
        return;
    }

    Json::Reader reader;
    Json::Value canceledJob;

    if ( reader.parse( std::string( buf, buf + size ), canceledJob ) )
    {
        std::string jobId;

        if ( canceledJob["jobId"].empty() )
        {
            FWE_LOG_ERROR( "Null or empty jobId in the received cancel request message" );
            return;
        }

        if ( !canceledJob["jobId"].isString() )
        {
            FWE_LOG_ERROR( "The jobId in the cancel request message is not a string" );
            return;
        }

        jobId = canceledJob["jobId"].asString();

        if ( jobId.empty() )
        {
            // This should never happen since IoT Jobs should never send us "" as a jobId
            FWE_LOG_ERROR( "The jobId received is an empty string" );
            return;
        }

        std::unique_lock<std::mutex> lock( mCampaignMutex );

        if ( mJobToStatus.find( jobId ) == mJobToStatus.end() )
        {
            // coverity[LOCK:FALSE] unique_lock's destructor won't try to unlock again
            lock.unlock();
            FWE_LOG_ERROR( "The jobId of the received canceled job is not running on this device" )
            return;
        }

        if ( mJobToCampaignId.find( jobId ) == mJobToCampaignId.end() )
        {
            // coverity[LOCK:FALSE] unique_lock's destructor won't try to unlock again
            lock.unlock();
            FWE_LOG_ERROR( "The jobId of the received canceled job is not running on this device" )
            return;
        }

        // Stop uploading data
        const auto &campaignId = mJobToCampaignId[jobId];

        // coverity[LOCK:FALSE] unique_lock's destructor won't try to unlock again
        lock.unlock();

        auto pIDs = mStreamManager.getPartitionIdsFromCampaign( campaignId );
        for ( auto pID : pIDs )
        {
            mStreamForwarder.cancelForward(
                campaignId, pID, Aws::IoTFleetWise::Store::StreamForwarder::Source::IOT_JOB );
        }

        lock.lock();
        mJobToStatus.erase( jobId );
        // we don't want to erase the jobId to CampaignId until after we cancelForward
        mJobToCampaignId.erase( jobId );
        // coverity[LOCK:FALSE] unique_lock's destructor won't try to unlock again
        lock.unlock();

        // we cannot update the job status to "CANCELED" since this will return an InvalidStateTransition code
    }
}

void
IoTJobsDataRequestHandler::onJobUploadCompleted( Store::CampaignID campaignId )
{
    FWE_LOG_INFO( "Received notification that a job has completed uploading data" );

    std::set<std::string> jobIds;
    {
        std::lock_guard<std::mutex> lock( mCampaignMutex );
        auto it = mJobToCampaignId.begin();
        while ( it != mJobToCampaignId.end() )
        {
            if ( it->second == campaignId )
            {
                mJobToStatus[it->first] = jobStatus::SUCCEEDED;
                jobIds.insert( it->first );
                it = mJobToCampaignId.erase( it );
            }
            else
            {
                it++;
            }
        }
    }

    if ( jobIds.empty() )
    {
        FWE_LOG_ERROR( "No jobId corresponds to campaign " + campaignId );
        return;
    }

    for ( const auto &jobId : jobIds )
    {
        Json::StreamWriterBuilder builder;
        Json::Value updateJobExecution;
        updateJobExecution["status"] = "SUCCEEDED";
        // setting clientToken as the jobId so that we can access the jobId in the rejected topic
        updateJobExecution["clientToken"] = jobId;

        const std::string data = Json::writeString( builder, updateJobExecution );
        // Publishing is async and non-blocking
        std::string topic = mMqttSender.getTopicConfig().updateJobExecutionTopic( jobId );
        mMqttSender.sendBuffer(
            topic, reinterpret_cast<const uint8_t *>( data.c_str() ), data.size(), [topic]( ConnectivityError result ) {
                if ( result == ConnectivityError::Success )
                {
                    FWE_LOG_TRACE( "Successfully sent message on topic " + topic );
                }
                else
                {
                    FWE_LOG_ERROR( "Send error " + std::to_string( static_cast<uint32_t>( result ) ) );
                }
            } );
    }
}

std::string
IoTJobsDataRequestHandler::random_string( size_t length )
{
    const std::string ALPHANUMERIC = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::random_device random_device;
    std::mt19937 engine( random_device() );
    std::uniform_int_distribution<size_t> distribution( 0, ALPHANUMERIC.size() - 1 );
    std::string output;
    for ( size_t i = 0; i < length; i++ )
    {
        // coverity[cert_str53_cpp_violation] Accessing element of string "ALPHANUMERIC" with index
        // "distribution(engine)" without range check.
        auto index = distribution( engine );
        if ( index < ALPHANUMERIC.length() )
        {
            output += ALPHANUMERIC[index];
        }
    }
    return output;
}

void
IoTJobsDataRequestHandler::sendGetPendingExecutions()
{
    Json::StreamWriterBuilder builder;
    Json::Value pendingJobs;
    pendingJobs["clientToken"] = random_string( RANDOM_STRING_SIZE );
    const std::string data = Json::writeString( builder, pendingJobs );
    mMqttSender.sendBuffer( mMqttSender.getTopicConfig().getPendingJobExecutionsTopic,
                            reinterpret_cast<const uint8_t *>( data.c_str() ),
                            data.size(),
                            []( ConnectivityError result ) {
                                if ( result == ConnectivityError::Success )
                                {
                                    FWE_LOG_TRACE( "Successfully requested pending jobs" );
                                }
                                else
                                {
                                    FWE_LOG_ERROR( "Send error " + std::to_string( static_cast<uint32_t>( result ) ) );
                                }
                            } );
}

uint64_t
IoTJobsDataRequestHandler::convertEndTimeToMS( const std::string &iso8601 )
{
    std::tm tmTime = {};
    std::stringstream ss( iso8601 );
    ss >> std::get_time( &tmTime, "%Y-%m-%dT%H:%M:%SZ" );

    if ( ss.fail() )
    {
        FWE_LOG_ERROR( "Malformed IoT Job endTime: " + iso8601 + ". The endTime will be set to 0 (default)" );
        return 0;
    }

    boost::posix_time::ptime time = boost::posix_time::ptime_from_tm( tmTime );
    boost::posix_time::ptime epoch( boost::gregorian::date( 1970, 1, 1 ) );
    boost::posix_time::time_duration duration = time - epoch;
    return static_cast<uint64_t>( duration.total_milliseconds() );
}

} // namespace IoTFleetWise
} // namespace Aws
