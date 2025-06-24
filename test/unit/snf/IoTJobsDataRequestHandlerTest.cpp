// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/IoTJobsDataRequestHandler.h"
#include "MqttClientWrapperMock.h"
#include "SenderMock.h"
#include "StreamForwarderMock.h"
#include "StreamManagerMock.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/AwsBootstrap.h"
#include "aws/iotfleetwise/AwsIotConnectivityModule.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/TelemetryDataSender.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include "aws/iotfleetwise/snf/RateLimiter.h"
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#include <atomic>
#include <aws/common/error.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::A;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::StrictMock;

class IoTJobsDataRequestHandlerTest : public ::testing::Test
{
protected:
    IoTJobsDataRequestHandlerTest()
        : mTopicConfig( "clientIdTest", TopicConfigArgs() )
        , mMqttSender( mTopicConfig )
        , mTelemetryDataSender(
              [this]() -> ISender & {
                  EXPECT_CALL( mMqttSender, getMaxSendSize() )
                      .Times( AnyNumber() )
                      .WillRepeatedly( Return( MAXIMUM_PAYLOAD_SIZE ) );
                  return mMqttSender;
              }(),
              std::make_unique<DataSenderProtoWriter>( mCANIDTranslator, nullptr ),
              mPayloadAdaptionConfigUncompressed,
              mPayloadAdaptionConfigCompressed )
        , mStreamForwarder( mStreamManager, mTelemetryDataSender, mRateLimiter )

    {
    }

    void
    SetUp() override
    {
        // Need to initialize the SDK to get proper error strings
        AwsBootstrap::getInstance().getClientBootStrap();

        mMqttClientWrapperMock = std::make_shared<StrictMock<MqttClientWrapperMock>>();
        EXPECT_CALL( *mMqttClientWrapperMock, MockedOperatorBool() )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( true ) );
        ON_CALL( *mMqttClientWrapperMock, MockedStart() ).WillByDefault( Invoke( [this]() noexcept -> bool {
            Aws::Crt::Mqtt5::OnConnectionSuccessEventData eventData;
            mMqttClientBuilderWrapperMock.mOnConnectionSuccessCallback( eventData );
            return true;
        } ) );
        ON_CALL( *mMqttClientWrapperMock, Stop( _ ) )
            .WillByDefault( Invoke(
                [this]( std::shared_ptr<Aws::Crt::Mqtt5::DisconnectPacket> disconnectOptions ) noexcept -> bool {
                    static_cast<void>( disconnectOptions );
                    Aws::Crt::Mqtt5::OnStoppedEventData eventData;
                    mMqttClientBuilderWrapperMock.mOnStoppedCallback( eventData );
                    return true;
                } ) );
        ON_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) )
            .WillByDefault(
                Invoke( [this]( std::shared_ptr<Aws::Crt::Mqtt5::SubscribePacket>,
                                Aws::Crt::Mqtt5::OnSubscribeCompletionHandler onSubscribeCompletionCallback ) -> bool {
                    onSubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                    mSubscribeCount++;
                    return true;
                } ) );

        ON_CALL( mMqttClientBuilderWrapperMock, Build() ).WillByDefault( Return( mMqttClientWrapperMock ) );
        ON_CALL( mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) )
            .WillByDefault( ReturnRef( mMqttClientBuilderWrapperMock ) );
        ON_CALL( mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) )
            .WillByDefault( DoAll( SaveArg<0>( &mConnectPacket ), ReturnRef( mMqttClientBuilderWrapperMock ) ) );
        ON_CALL( mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) )
            .WillByDefault( ReturnRef( mMqttClientBuilderWrapperMock ) );
        ON_CALL( mMqttClientBuilderWrapperMock, WithSessionBehavior( _ ) )
            .WillByDefault( ReturnRef( mMqttClientBuilderWrapperMock ) );
        ON_CALL( mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) )
            .WillByDefault( ReturnRef( mMqttClientBuilderWrapperMock ) );
        ON_CALL( mMqttClientBuilderWrapperMock, WithCertificateAuthority( _ ) )
            .WillByDefault( ReturnRef( mMqttClientBuilderWrapperMock ) );

        EXPECT_CALL( mMqttClientBuilderWrapperMock, WithClientExtendedValidationAndFlowControl( _ ) ).Times( 1 );
        EXPECT_CALL( mMqttClientBuilderWrapperMock, WithConnectOptions( _ ) ).Times( 1 );
        EXPECT_CALL( mMqttClientBuilderWrapperMock, WithOfflineQueueBehavior( _ ) ).Times( 1 );
        EXPECT_CALL( mMqttClientBuilderWrapperMock, WithPingTimeoutMs( _ ) ).Times( 1 );
        EXPECT_CALL( mMqttClientBuilderWrapperMock, Build() ).Times( 1 );
        mConnectivityModule = std::make_unique<AwsIotConnectivityModule>(
            "", "clientIdTest", mMqttClientBuilderWrapperMock, mTopicConfig );
    }

    std::shared_ptr<StrictMock<MqttClientWrapperMock>> mMqttClientWrapperMock;
    StrictMock<MqttClientBuilderWrapperMock> mMqttClientBuilderWrapperMock;
    TopicConfig mTopicConfig;
    std::unique_ptr<AwsIotConnectivityModule> mConnectivityModule;
    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> mConnectPacket;
    std::atomic<int> mSubscribeCount{ 0 };

    CANInterfaceIDTranslator mCANIDTranslator;
    StrictMock<Testing::SenderMock> mMqttSender;
    PayloadAdaptionConfig mPayloadAdaptionConfigUncompressed{ 80, 70, 90, 10 };
    PayloadAdaptionConfig mPayloadAdaptionConfigCompressed{ 80, 70, 90, 10 };
    TelemetryDataSender mTelemetryDataSender;
    StrictMock<Testing::StreamManagerMock> mStreamManager;
    RateLimiter mRateLimiter;
    StrictMock<Testing::StreamForwarderMock> mStreamForwarder;

    std::shared_ptr<IReceiver> mReceiverIotJob;
    std::shared_ptr<IReceiver> mReceiverJobDocumentAccepted;
    std::shared_ptr<IReceiver> mReceiverJobDocumentRejected;
    std::shared_ptr<IReceiver> mReceiverPendingJobsAccepted;
    std::shared_ptr<IReceiver> mReceiverPendingJobsRejected;
    std::shared_ptr<IReceiver> mReceiverUpdateIotJobStatusAccepted;
    std::shared_ptr<IReceiver> mReceiverUpdateIotJobStatusRejected;
    std::shared_ptr<IReceiver> mReceiverCanceledIoTJobs;
    std::string thingName = "clientIdTest";
    static constexpr unsigned MAXIMUM_PAYLOAD_SIZE = 400;
};

TEST_F( IoTJobsDataRequestHandlerTest, IoTJobsDataRequestHandler )
{
    EXPECT_CALL( mStreamForwarder, registerJobCompletionCallback( _ ) )
        .Times( 1 )
        .WillRepeatedly( Invoke( []( Store::StreamForwarder::JobCompletionCallback jobCompletionCallback ) -> void {
            jobCompletionCallback( "test" );
        } ) );

    mReceiverIotJob = mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/notify" );
    mReceiverJobDocumentAccepted =
        mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/+/get/accepted" );
    mReceiverJobDocumentRejected =
        mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/+/get/rejected" );
    mReceiverPendingJobsAccepted =
        mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/get/accepted" );
    mReceiverPendingJobsRejected =
        mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/get/rejected" );
    mReceiverUpdateIotJobStatusAccepted =
        mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/+/update/accepted" );
    mReceiverUpdateIotJobStatusRejected =
        mConnectivityModule->createReceiver( "$aws/things/" + thingName + "/jobs/+/update/rejected" );
    mReceiverCanceledIoTJobs = mConnectivityModule->createReceiver( "$aws/events/job/+/cancellation_in_progress" );
    IoTJobsDataRequestHandler mIoTJobsDataRequestHandler( mMqttSender,
                                                          *mReceiverIotJob,
                                                          *mReceiverJobDocumentAccepted,
                                                          *mReceiverJobDocumentRejected,
                                                          *mReceiverPendingJobsAccepted,
                                                          *mReceiverPendingJobsRejected,
                                                          *mReceiverUpdateIotJobStatusAccepted,
                                                          *mReceiverUpdateIotJobStatusRejected,
                                                          *mReceiverCanceledIoTJobs,
                                                          mStreamManager,
                                                          mStreamForwarder,
                                                          thingName );

    EXPECT_CALL( *mMqttClientWrapperMock, MockedStart() ).Times( 1 );
    EXPECT_CALL( *mMqttClientWrapperMock, Subscribe( _, _ ) ).Times( 8 );

    ASSERT_TRUE( mConnectivityModule->connect() );

    WAIT_ASSERT_TRUE( mConnectivityModule->isAlive() );
    WAIT_ASSERT_EQ( mSubscribeCount.load(), 8 );

    EXPECT_CALL( mStreamManager, hasCampaign( _ ) ).Times( AnyNumber() ).WillRepeatedly( Return( true ) );

    auto publishTime = ClockHandler::getClock()->monotonicTimeSinceEpochMs();

    // Test GetPendingJobExecutions Accepted

    Json::StreamWriterBuilder builder;
    Json::Value getPendingJobsAccepted1;
    getPendingJobsAccepted1["jobId"] = "1";
    getPendingJobsAccepted1["queuedAt"] = publishTime;
    getPendingJobsAccepted1["lastUpdatedAt"] = publishTime;
    getPendingJobsAccepted1["versionNumber"] = 1;

    Json::Value getPendingJobsAccepted2;
    getPendingJobsAccepted2["jobId"] = "2";
    getPendingJobsAccepted2["queuedAt"] = publishTime;
    getPendingJobsAccepted2["lastUpdatedAt"] = publishTime;
    getPendingJobsAccepted2["versionNumber"] = 1;

    Json::Value noJobIdPendingJobsAccepted;
    noJobIdPendingJobsAccepted["queuedAt"] = publishTime;
    noJobIdPendingJobsAccepted["lastUpdatedAt"] = publishTime;
    noJobIdPendingJobsAccepted["versionNumber"] = 1;

    Json::Value invalidJobIdPendingJobsAccepted;
    invalidJobIdPendingJobsAccepted["jobId"] = 1;
    invalidJobIdPendingJobsAccepted["queuedAt"] = publishTime;
    invalidJobIdPendingJobsAccepted["lastUpdatedAt"] = publishTime;
    invalidJobIdPendingJobsAccepted["versionNumber"] = 1;

    Json::Value nullJobIdPendingJobsAccepted;
    nullJobIdPendingJobsAccepted["jobId"] = Json::Value::null;
    nullJobIdPendingJobsAccepted["queuedAt"] = publishTime;
    nullJobIdPendingJobsAccepted["lastUpdatedAt"] = publishTime;
    nullJobIdPendingJobsAccepted["versionNumber"] = 1;

    Json::Value pendingJobsMocked;
    pendingJobsMocked["timestamp"] = publishTime;
    pendingJobsMocked["queuedJobs"] = Json::Value( Json::arrayValue );
    pendingJobsMocked["inProgressJobs"] = Json::Value( Json::arrayValue );
    pendingJobsMocked["inProgressJobs"].append( getPendingJobsAccepted1 );
    pendingJobsMocked["inProgressJobs"].append( noJobIdPendingJobsAccepted );
    pendingJobsMocked["inProgressJobs"].append( invalidJobIdPendingJobsAccepted );
    pendingJobsMocked["inProgressJobs"].append( nullJobIdPendingJobsAccepted );
    pendingJobsMocked["queuedJobs"].append( getPendingJobsAccepted2 );
    pendingJobsMocked["queuedJobs"].append( noJobIdPendingJobsAccepted );
    pendingJobsMocked["queuedJobs"].append( invalidJobIdPendingJobsAccepted );
    pendingJobsMocked["queuedJobs"].append( nullJobIdPendingJobsAccepted );

    const std::string pendingJobData = Json::writeString( builder, pendingJobsMocked );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( pendingJobData.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/1/get", _, _, _ ) ).Times( 1 );
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/2/get", _, _, _ ) ).Times( 1 );
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Two valid pending jobs so there should be two job document requests
    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/get" ).size(), 1 );
    auto sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/get" );

    Json::Value expectedJobDocRequest1;
    expectedJobDocRequest1["jobId"] = "1";
    expectedJobDocRequest1["thingName"] = thingName;
    expectedJobDocRequest1["includeJobDocument"] = true;

    Json::Reader reader;
    Json::Value actualJobDocRequest1;

    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobDocRequest1 ) );
    ASSERT_EQ( actualJobDocRequest1, expectedJobDocRequest1 );

    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/2/get" ).size(), 1 );
    sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/2/get" );

    Json::Value expectedJobDocRequest2;
    expectedJobDocRequest2["jobId"] = "2";
    expectedJobDocRequest2["thingName"] = thingName;
    expectedJobDocRequest2["includeJobDocument"] = true;

    Json::Value actualJobDocRequest2;

    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobDocRequest2 ) );
    ASSERT_EQ( actualJobDocRequest2, expectedJobDocRequest2 );

    // Clear sent buffer data mock so that we can test updating the job status
    mMqttSender.clearSentBufferData();

    Json::Value pendingJobsMockedBad1;
    pendingJobsMockedBad1["timestamp"] = publishTime;
    pendingJobsMockedBad1["queuedJobs"] = "";

    Json::Value pendingJobsMockedBad2;
    pendingJobsMockedBad2["timestamp"] = publishTime;
    pendingJobsMockedBad2["inProgressJobs"] = "";

    const std::string pendingJobBad1 = Json::writeString( builder, pendingJobsMockedBad1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( pendingJobBad1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string pendingJobBad2 = Json::writeString( builder, pendingJobsMockedBad2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( pendingJobBad2.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Two invalid pending jobs so there should be zero job document requests
    ASSERT_EQ( mMqttSender.getSentBufferData().size(), 0 );

    // Test GetPendingJobExecutions Rejected
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( pendingJobData.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/get", _, _, _ ) ).Times( 1 );
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // One pending job so there should be one job document request after retrying GetPendingJobExecutions
    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/get" ).size(), 1 );

    // Clear sent buffer data mock so that we can test updating the job status
    mMqttSender.clearSentBufferData();

    // Test JobExecutionChanged (notify topic)

    Json::Value job1;
    job1["jobId"] = "1";
    job1["queuedAt"] = publishTime;
    job1["lastUpdatedAt"] = publishTime;
    job1["versionNumber"] = 1;

    Json::Value job2;
    job2["jobId"] = "2";
    job2["queuedAt"] = publishTime;
    job2["lastUpdatedAt"] = publishTime;
    job2["versionNumber"] = 1;

    Json::Value mockJob;
    mockJob["timestamp"] = publishTime;
    mockJob["jobs"]["QUEUED"] = Json::Value( Json::arrayValue );
    mockJob["jobs"]["QUEUED"].append( job1 );
    mockJob["jobs"]["QUEUED"].append( job2 );

    const std::string data = Json::writeString( builder, mockJob );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/notify",
                                                              Aws::Crt::ByteCursorFromCString( data.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/1/get", _, _, _ ) ).Times( 1 );
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/2/get", _, _, _ ) ).Times( 1 );
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Two valid jobs so there should be two job document requests
    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/get" ).size(), 1 );
    sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/get" );

    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobDocRequest1 ) );
    ASSERT_EQ( actualJobDocRequest1, expectedJobDocRequest1 );

    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/2/get" ).size(), 1 );
    sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/2/get" );

    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobDocRequest2 ) );
    ASSERT_EQ( actualJobDocRequest2, expectedJobDocRequest2 );

    // Clear sent buffer data mock so that we can test updating the job status
    mMqttSender.clearSentBufferData();

    Json::Value badJob1;
    badJob1["timestamp"] = publishTime;

    Json::Value badJob2;
    badJob2["timestamp"] = publishTime;
    badJob2["jobs"]["QUEUED"] = Json::Value( Json::arrayValue );

    Json::Value badJob3;
    badJob3["timestamp"] = publishTime;
    badJob3["jobs"]["QUEUED"] = "QUEUED should be a list";

    const std::string bad1 = Json::writeString( builder, badJob1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/notify",
                                                              Aws::Crt::ByteCursorFromCString( bad1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string bad2 = Json::writeString( builder, badJob2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/notify",
                                                              Aws::Crt::ByteCursorFromCString( bad2.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string bad3 = Json::writeString( builder, badJob3 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/notify",
                                                              Aws::Crt::ByteCursorFromCString( bad3.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Received 3 malformed jobs so there should be 0 JobDocumentRequests
    ASSERT_EQ( mMqttSender.getSentBufferData().size(), 0 );

    Json::Value invalidJson;
    // No jobId key
    invalidJson["badJobIdKey"] = "3";
    invalidJson["queuedAt"] = publishTime;
    invalidJson["lastUpdatedAt"] = publishTime;
    invalidJson["versionNumber"] = 1;

    Json::Value unexpectedFormat;
    // jobId should be a string, not an int
    unexpectedFormat["jobId"] = 4;
    unexpectedFormat["queuedAt"] = publishTime;
    unexpectedFormat["lastUpdatedAt"] = publishTime;
    unexpectedFormat["versionNumber"] = 1;

    Json::Value nullJobId;
    // jobId is null
    nullJobId["jobId"] = Json::Value::null;
    nullJobId["queuedAt"] = publishTime;
    nullJobId["lastUpdatedAt"] = publishTime;
    nullJobId["versionNumber"] = 1;

    Json::Value emptyJobId;
    // jobId is ""
    emptyJobId["jobId"] = "";
    emptyJobId["queuedAt"] = publishTime;
    emptyJobId["lastUpdatedAt"] = publishTime;
    emptyJobId["versionNumber"] = 1;

    Json::Value invalidJob;
    invalidJob["timestamp"] = publishTime;
    invalidJob["jobs"]["QUEUED"] = Json::Value( Json::arrayValue );
    invalidJob["jobs"]["QUEUED"].append( invalidJson );
    invalidJob["jobs"]["QUEUED"].append( unexpectedFormat );
    invalidJob["jobs"]["QUEUED"].append( nullJobId );
    invalidJob["jobs"]["QUEUED"].append( emptyJobId );

    const std::string invalidJsonData = Json::writeString( builder, invalidJob );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/notify",
            Aws::Crt::ByteCursorFromCString( invalidJsonData.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Four invalid jobs so there should be zero document requests
    ASSERT_EQ( mMqttSender.getSentBufferData().size(), 0 );

    Json::Value emptyQueue;
    emptyQueue["timestamp"] = publishTime;
    emptyQueue["jobs"]["QUEUED"] = Json::Value( Json::arrayValue );

    Json::Value queueNotAList;
    queueNotAList["timestamp"] = publishTime;
    queueNotAList["jobs"]["QUEUED"] = "QUEUED should be a list";

    const std::string emptyQueueJsonData = Json::writeString( builder, emptyQueue );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/notify",
            Aws::Crt::ByteCursorFromCString( emptyQueueJsonData.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Invalid job so there should be zero document requests
    ASSERT_EQ( mMqttSender.getSentBufferData().size(), 0 );

    const std::string invalidQueueJsonData = Json::writeString( builder, queueNotAList );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/notify",
            Aws::Crt::ByteCursorFromCString( invalidQueueJsonData.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Invalid job so there should be zero document requests
    ASSERT_EQ( mMqttSender.getSentBufferData().size(), 0 );

    // Test DescribeJobExecution Accepted and Rejected

    Json::Value invalidJobDoc1;
    // no execution field
    invalidJobDoc1["clientToken"] = thingName;
    invalidJobDoc1["timestamp"] = publishTime;

    Json::Value invalidJobDoc2;
    // null execution field
    invalidJobDoc2["clientToken"] = thingName;
    invalidJobDoc2["timestamp"] = publishTime;
    invalidJobDoc2["execution"] = Json::Value::null;

    Json::Value invalidJobDoc3;
    // no jobId field
    invalidJobDoc3["clientToken"] = thingName;
    invalidJobDoc3["timestamp"] = publishTime;
    invalidJobDoc3["execution"]["status"] = "QUEUED";

    Json::Value invalidJobDoc4;
    // invalid jobId field
    invalidJobDoc4["clientToken"] = thingName;
    invalidJobDoc4["timestamp"] = publishTime;
    invalidJobDoc4["execution"]["status"] = "QUEUED";
    invalidJobDoc4["execution"]["jobId"] = 1;

    Json::Value invalidJobDoc5;
    // invalid jobId field
    invalidJobDoc5["clientToken"] = thingName;
    invalidJobDoc5["timestamp"] = publishTime;
    invalidJobDoc5["execution"]["status"] = "QUEUED";
    invalidJobDoc5["execution"]["jobId"] = "";

    const std::string badDataReq1 = Json::writeString( builder, invalidJobDoc1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string badDataReq2 = Json::writeString( builder, invalidJobDoc2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq2.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq2.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string badDataReq3 = Json::writeString( builder, invalidJobDoc3 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq3.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq3.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string badDataReq4 = Json::writeString( builder, invalidJobDoc4 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq4.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq4.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string badDataReq5 = Json::writeString( builder, invalidJobDoc5 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq5.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( badDataReq5.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // There should be no update job status requests since all the job docs were invalid
    ASSERT_EQ( mMqttSender.getSentBufferData().size(), 0 );

    Json::Value docRequest1;
    docRequest1["clientToken"] = thingName;
    docRequest1["timestamp"] = publishTime;
    docRequest1["execution"]["approximateSecondsBeforeTimedOut"] = 10;
    docRequest1["execution"]["jobId"] = "1";
    docRequest1["execution"]["status"] = "QUEUED";
    docRequest1["execution"]["queuedAt"] = publishTime;
    docRequest1["execution"]["lastUpdatedAt"] = publishTime;
    docRequest1["execution"]["versionNumber"] = 1;
    docRequest1["execution"]["jobDocument"]["versionNumber"] = 1;
    docRequest1["execution"]["jobDocument"]["parameters"]["campaignArn"] = "garbage_data";
    docRequest1["execution"]["jobDocument"]["parameters"]["endTime"] = publishTime;

    Json::Value docRequest2;
    docRequest2["clientToken"] = thingName;
    docRequest2["timestamp"] = publishTime;
    docRequest2["execution"]["approximateSecondsBeforeTimedOut"] = 10;
    docRequest2["execution"]["jobId"] = "2";
    docRequest2["execution"]["status"] = "IN_PROGRESS";
    docRequest2["execution"]["queuedAt"] = publishTime;
    docRequest2["execution"]["lastUpdatedAt"] = publishTime;
    docRequest2["execution"]["versionNumber"] = 1;
    docRequest2["execution"]["jobDocument"]["versionNumber"] = 1;
    docRequest2["execution"]["jobDocument"]["parameters"]["campaignArn"] = "garbage_data";
    docRequest2["execution"]["jobDocument"]["parameters"]["endTime"] = publishTime;

    Json::Value docRequest3;
    // no campaignArn
    docRequest3["clientToken"] = thingName;
    docRequest3["timestamp"] = publishTime;
    docRequest3["execution"]["approximateSecondsBeforeTimedOut"] = 10;
    docRequest3["execution"]["jobId"] = "3";
    docRequest3["execution"]["status"] = "QUEUED";
    docRequest3["execution"]["queuedAt"] = publishTime;
    docRequest3["execution"]["lastUpdatedAt"] = publishTime;
    docRequest3["execution"]["versionNumber"] = 1;
    docRequest3["execution"]["jobDocument"]["versionNumber"] = 1;
    docRequest3["execution"]["jobDocument"]["parameters"]["endTime"] = publishTime;

    Json::Value docRequest4;
    // no parameters
    docRequest4["clientToken"] = thingName;
    docRequest4["timestamp"] = publishTime;
    docRequest4["execution"]["approximateSecondsBeforeTimedOut"] = 10;
    docRequest4["execution"]["jobId"] = "3";
    docRequest4["execution"]["status"] = "QUEUED";
    docRequest4["execution"]["queuedAt"] = publishTime;
    docRequest4["execution"]["lastUpdatedAt"] = publishTime;
    docRequest4["execution"]["versionNumber"] = 1;
    docRequest4["execution"]["jobDocument"]["versionNumber"] = 1;

    Json::Value docRequest5;
    // no parameters
    docRequest5["clientToken"] = thingName;
    docRequest5["timestamp"] = publishTime;
    docRequest5["execution"]["approximateSecondsBeforeTimedOut"] = 10;
    docRequest5["execution"]["jobId"] = "3";
    docRequest5["execution"]["status"] = "IN_PROGRESS";
    docRequest5["execution"]["queuedAt"] = publishTime;
    docRequest5["execution"]["lastUpdatedAt"] = publishTime;
    docRequest5["execution"]["versionNumber"] = 1;
    docRequest5["execution"]["jobDocument"]["versionNumber"] = 1;
    docRequest5["execution"]["jobDocument"]["parameters"]["campaignArn"] = "garbage_data";
    docRequest5["execution"]["jobDocument"]["parameters"]["endTime"] = publishTime;

    const std::string dataReq1 = Json::writeString( builder, docRequest1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( dataReq1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/1/update", _, _, _ ) ).Times( 1 );
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string dataReq2 = Json::writeString( builder, docRequest2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/2/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( dataReq2.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Two jobs, but only 1 of them is QUEUED so there should be 1 update event
    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/update" ).size(), 1 );
    sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/update" );

    Json::Value expectedJobUpdateStatus1;
    expectedJobUpdateStatus1["status"] = "IN_PROGRESS";
    expectedJobUpdateStatus1["clientToken"] = "1";

    Json::Value actualJobUpdateStatus1;

    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobUpdateStatus1 ) );
    ASSERT_EQ( actualJobUpdateStatus1, expectedJobUpdateStatus1 );

    mMqttSender.clearSentBufferData();

    const std::string dataReq3 = Json::writeString( builder, docRequest3 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/3/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( dataReq3.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string dataReq4 = Json::writeString( builder, docRequest4 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/3/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( dataReq4.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    EXPECT_CALL( mStreamManager, hasCampaign( _ ) ).Times( AnyNumber() ).WillRepeatedly( Return( false ) );

    const std::string dataReq5 = Json::writeString( builder, docRequest5 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/3/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( dataReq5.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/3/update", _, _, _ ) ).Times( 1 );
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/accepted",
                                                              Aws::Crt::ByteCursorFromCString( dataReq1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        EXPECT_CALL( mMqttSender, mockedSendBuffer( "$aws/things/clientIdTest/jobs/1/update", _, _, _ ) ).Times( 1 );
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Two valid job docs so there should be two update events
    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/update" ).size(), 1 );
    sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/update" );

    Json::Value expectedJobUpdateStatus6;
    expectedJobUpdateStatus6["status"] = "REJECTED";
    expectedJobUpdateStatus6["clientToken"] = "1";

    Json::Value actualJobUpdateStatus6;

    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobUpdateStatus6 ) );
    ASSERT_EQ( actualJobUpdateStatus6, expectedJobUpdateStatus6 );

    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/3/update" ).size(), 1 );
    sentBufferData = mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/3/update" );

    Json::Value expectedJobUpdateStatus5;
    expectedJobUpdateStatus5["status"] = "REJECTED";
    expectedJobUpdateStatus5["clientToken"] = "3";

    Json::Value actualJobUpdateStatus5;
    ASSERT_TRUE( reader.parse( sentBufferData[0].data, actualJobUpdateStatus5 ) );
    ASSERT_EQ( actualJobUpdateStatus5, expectedJobUpdateStatus5 );

    mMqttSender.clearSentBufferData();

    // Test DescribeJobExecution rejected
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/get/rejected",
                                                              Aws::Crt::ByteCursorFromCString( dataReq1.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Test UpdateJobExecution retry
    const std::string updateRejected = Json::writeString( builder, expectedJobUpdateStatus1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket =
            std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( "$aws/things/clientIdTest/jobs/1/update/rejected",
                                                              Aws::Crt::ByteCursorFromCString( updateRejected.c_str() ),
                                                              Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    Json::Value badUpdateStatus1;
    badUpdateStatus1["status"] = "IN_PROGRESS";

    const std::string updateRejected1 = Json::writeString( builder, badUpdateStatus1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/1/update/rejected",
            Aws::Crt::ByteCursorFromCString( updateRejected1.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    Json::Value badUpdateStatus2;
    badUpdateStatus2["status"] = "IN_PROGRESS";
    badUpdateStatus2["clientToken"] = 1;

    const std::string updateRejected2 = Json::writeString( builder, badUpdateStatus2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/1/update/rejected",
            Aws::Crt::ByteCursorFromCString( updateRejected2.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    Json::Value badUpdateStatus3;
    badUpdateStatus3["status"] = "IN_PROGRESS";
    badUpdateStatus3["clientToken"] = "";

    const std::string updateRejected3 = Json::writeString( builder, badUpdateStatus3 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/1/update/rejected",
            Aws::Crt::ByteCursorFromCString( updateRejected3.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // We are not retrying rejected update requests so the update status sender should have sent 0 messages
    ASSERT_EQ( mMqttSender.getSentBufferDataByTopic( "$aws/things/clientIdTest/jobs/1/update" ).size(), 0 );

    // Test UpdateJobExecution accepted
    const std::string updateAccepted1 = Json::writeString( builder, badUpdateStatus1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/1/update/accepted",
            Aws::Crt::ByteCursorFromCString( updateAccepted1.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string updateAccepted2 = Json::writeString( builder, badUpdateStatus2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/1/update/accepted",
            Aws::Crt::ByteCursorFromCString( updateAccepted2.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string updateAccepted3 = Json::writeString( builder, badUpdateStatus3 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/things/clientIdTest/jobs/1/update/accepted",
            Aws::Crt::ByteCursorFromCString( updateAccepted3.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    // Test onCanceledJobReceived
    // TODO: Finish canceledJob testing once the cancel job handler is completed
    Json::Value canceledJob1;
    canceledJob1["jobId"] = "1";

    Json::Value canceledJob2;
    canceledJob2["jobId"] = 2;

    Json::Value canceledJob3;
    canceledJob3["jobId"] = "";

    const std::string canceledJobData1 = Json::writeString( builder, canceledJob1 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/events/job/1/cancellation_in_progress",
            Aws::Crt::ByteCursorFromCString( canceledJobData1.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string canceledJobData2 = Json::writeString( builder, canceledJob2 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/events/job/1/cancellation_in_progress",
            Aws::Crt::ByteCursorFromCString( canceledJobData2.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    const std::string canceledJobData3 = Json::writeString( builder, canceledJob3 );
    {
        Aws::Crt::Mqtt5::PublishReceivedEventData eventData;

        auto publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            "$aws/events/job/1/cancellation_in_progress",
            Aws::Crt::ByteCursorFromCString( canceledJobData3.c_str() ),
            Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
        eventData.publishPacket = publishPacket;
        mMqttClientBuilderWrapperMock.mOnPublishReceivedHandlerCallback( eventData );
    }

    uint64_t endTime = IoTJobsDataRequestHandler::convertEndTimeToMS( "2024-03-05T23:00:00Z" );
    uint64_t expectedEndTime = 1709679600000;

    uint64_t malformedEndTime = IoTJobsDataRequestHandler::convertEndTimeToMS( "2024-03-0523:00:00Z" );
    uint64_t expectedMalformedEndTime = 0;

    ASSERT_EQ( endTime, expectedEndTime );
    ASSERT_EQ( malformedEndTime, expectedMalformedEndTime );

    // Should be called on destruction
    EXPECT_CALL( *mMqttClientWrapperMock, Unsubscribe( _, _ ) )
        .Times( 8 )
        .WillRepeatedly( Invoke(
            []( std::shared_ptr<UnsubscribePacket>,
                Aws::Crt::Mqtt5::OnUnsubscribeCompletionHandler onUnsubscribeCompletionCallback ) noexcept -> bool {
                onUnsubscribeCompletionCallback( AWS_ERROR_SUCCESS, nullptr );
                return true;
            } ) );

    EXPECT_CALL( *mMqttClientWrapperMock, Stop( _ ) ).Times( 1 );

    ASSERT_TRUE( mConnectivityModule->disconnect() );
}

} // namespace IoTFleetWise
} // namespace Aws
