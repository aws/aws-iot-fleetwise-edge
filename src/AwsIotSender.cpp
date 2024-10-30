// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotSender.h"
#include "AwsSDKMemoryManager.h"
#include "IConnectionTypes.h"
#include "IConnectivityModule.h"
#include "LoggingModule.h"
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <functional>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

AwsIotSender::AwsIotSender( IConnectivityModule *connectivityModule,
                            std::shared_ptr<MqttClientWrapper> &mqttClient,
                            std::string topicName,
                            Aws::Crt::Mqtt5::QOS publishQoS )
    : mConnectivityModule( connectivityModule )
    , mMqttClient( mqttClient )
    , mTopicName( std::move( topicName ) )
    , mPublishQoS( publishQoS )
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
AwsIotSender::sendBuffer( const std::uint8_t *buf, size_t size, OnDataSentCallback callback )
{
    sendBufferToTopic( mTopicName, buf, size, callback );
}

void
AwsIotSender::sendBufferToTopic( const std::string &topic,
                                 const uint8_t *buf,
                                 size_t size,
                                 OnDataSentCallback callback )
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

    publishMessageToTopic( topic, buf, size, callback );
}

void
AwsIotSender::publishMessageToTopic( const std::string &topic,
                                     const uint8_t *buf,
                                     size_t size,
                                     OnDataSentCallback callback )
{
    auto payload = Aws::Crt::ByteBufFromArray( buf, size );

    auto onPublishComplete = [size, callback, this]( int errorCode,
                                                     std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> result ) mutable {
        {
            AwsSDKMemoryManager::getInstance().releaseReservedMemory( size );
        }

        if ( result->wasSuccessful() )
        {
            FWE_LOG_TRACE( "Publish succeeded" );
            mPayloadCountSent++;
            callback( ConnectivityError::Success );
        }
        else
        {
            auto errorString = Aws::Crt::ErrorDebugString( errorCode );
            FWE_LOG_ERROR( std::string( "Operation failed with error" ) +
                           ( errorString != nullptr ? std::string( errorString ) : std::string( "Unknown error" ) ) );
            callback( ConnectivityError::TransmissionError );
        }
    };

    std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket> publishPacket = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        topic.c_str(), Aws::Crt::ByteCursorFromByteBuf( payload ), mPublishQoS );
    if ( !mMqttClient->Publish( publishPacket, onPublishComplete ) )
    {
        callback( ConnectivityError::TransmissionError );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
