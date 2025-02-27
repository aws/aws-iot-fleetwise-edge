// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/AwsIotSender.h"
#include "aws/iotfleetwise/AwsSDKMemoryManager.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/IConnectivityModule.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <functional>

namespace Aws
{
namespace IoTFleetWise
{

AwsIotSender::AwsIotSender( const IConnectivityModule *connectivityModule,
                            MqttClientWrapper &mqttClient,
                            const TopicConfig &topicConfig )
    : mConnectivityModule( connectivityModule )
    , mMqttClient( mqttClient )
    , mTopicConfig( topicConfig )
{
}

bool
AwsIotSender::isAlive()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    return isAliveNotThreadSafe();
}

bool
AwsIotSender::isAliveNotThreadSafe()
{
    if ( mConnectivityModule == nullptr )
    {
        return false;
    }
    return mConnectivityModule->isAlive();
}

size_t
AwsIotSender::getMaxSendSize() const
{
    return AWS_IOT_MAX_MESSAGE_SIZE;
}

void
AwsIotSender::sendBuffer(
    const std::string &topic, const uint8_t *buf, size_t size, OnDataSentCallback callback, QoS qos )
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( topic.empty() )
    {
        FWE_LOG_WARN( "Invalid topic provided" );
        callback( ConnectivityError::NotConfigured );
        return;
    }

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_WARN( "No valid data provided" );
        callback( ConnectivityError::WrongInputData );
        return;
    }

    if ( size > getMaxSendSize() )
    {
        FWE_LOG_WARN( "Payload provided is too long" );
        callback( ConnectivityError::WrongInputData );
        return;
    }

    if ( !isAliveNotThreadSafe() )
    {
        callback( ConnectivityError::NoConnection );
        return;
    }

    if ( !AwsSDKMemoryManager::getInstance().reserveMemory( size ) )
    {
        FWE_LOG_ERROR( "Not sending out the message with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory." );

        callback( ConnectivityError::QuotaReached );
        return;
    }

    auto sdkQos = Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE;
    switch ( qos )
    {
    case QoS::AT_MOST_ONCE:
        sdkQos = Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE;
        break;
    case QoS::AT_LEAST_ONCE:
        sdkQos = Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE;
        break;
    }

    publishMessageToTopic( topic, buf, size, callback, sdkQos );
}

void
AwsIotSender::publishMessageToTopic(
    const std::string &topic, const uint8_t *buf, size_t size, OnDataSentCallback callback, Aws::Crt::Mqtt5::QOS qos )
{
    auto payload = Aws::Crt::ByteBufFromArray( buf, size );

    auto onPublishComplete =
        // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
        [size, callback, this]( int errorCode, std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> result ) mutable {
            {
                AwsSDKMemoryManager::getInstance().releaseReservedMemory( size );
            }

            if ( !result->wasSuccessful() )
            {
                auto errorString = Aws::Crt::ErrorDebugString( errorCode );
                auto logMessage =
                    "Publish failed with error: " +
                    ( errorString != nullptr ? std::string( errorString ) : std::string( "Unknown error" ) );
                if ( errorCode == AWS_ERROR_MQTT5_USER_REQUESTED_STOP )
                {
                    FWE_LOG_TRACE( logMessage );
                }
                else
                {
                    FWE_LOG_ERROR( logMessage );
                }
                callback( ConnectivityError::TransmissionError );
                return;
            }

            FWE_LOG_TRACE( "Publish succeeded" );
            mPayloadCountSent++;
            callback( ConnectivityError::Success );
        };

    std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket> publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        topic.c_str(), Aws::Crt::ByteCursorFromByteBuf( payload ), qos );
    if ( !mMqttClient.Publish( publishPacket, onPublishComplete ) )
    {
        callback( ConnectivityError::TransmissionError );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
