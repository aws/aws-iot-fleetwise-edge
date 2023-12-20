// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotChannel.h"
#include "AwsSDKMemoryManager.h"
#include "CacheAndPersist.h"
#include "IConnectivityModule.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <aws/crt/Api.h>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <future>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

AwsIotChannel::AwsIotChannel( IConnectivityModule *connectivityModule,
                              std::shared_ptr<PayloadManager> payloadManager,
                              std::shared_ptr<MqttClientWrapper> &mqttClient,
                              std::string topicName,
                              bool subscription )
    : mConnectivityModule( connectivityModule )
    , mPayloadManager( std::move( payloadManager ) )
    , mMqttClient( mqttClient )
    , mTopicName( std::move( topicName ) )
    , mSubscribed( false )
    , mSubscription( subscription )
{
}

bool
AwsIotChannel::isAlive()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    return isAliveNotThreadSafe();
}

bool
AwsIotChannel::isAliveNotThreadSafe()
{
    if ( mConnectivityModule == nullptr )
    {
        return false;
    }
    return mConnectivityModule->isAlive() && ( ( !mSubscription ) || mSubscribed );
}

ConnectivityError
AwsIotChannel::subscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( !isTopicValid() )
    {
        FWE_LOG_ERROR( "Empty ingestion topic name provided" );
        return ConnectivityError::NotConfigured;
    }
    if ( !mConnectivityModule->isAlive() )
    {
        FWE_LOG_ERROR( "MQTT Connection not established, failed to subscribe" );
        return ConnectivityError::NoConnection;
    }

    /*
     * Subscribe for incoming publish messages on topic.
     */
    std::promise<bool> subscribeFinishedPromise;
    auto onSubAck = [&]( int errorCode, std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> subAckPacket ) {
        mSubscribed = false;
        if ( errorCode != 0 )
        {
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
            auto errorString = Aws::Crt::ErrorDebugString( errorCode );
            FWE_LOG_ERROR( "Subscribe failed with error code " + std::to_string( errorCode ) + ": " +
                           std::string( errorString != nullptr ? errorString : "Unknown error" ) );
            subscribeFinishedPromise.set_value( false );
            return;
        }

        std::string grantedQoS = "unknown";
        if ( subAckPacket != nullptr )
        {
            for ( auto reasonCode : subAckPacket->getReasonCodes() )
            {
                if ( reasonCode <= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_GRANTED_QOS_2 )
                {
                    grantedQoS = std::to_string( reasonCode );
                }
                else if ( reasonCode >= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_UNSPECIFIED_ERROR )
                {
                    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
                    // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
                    auto reasonString = std::string( subAckPacket->getReasonString().has_value()
                                                         ? subAckPacket->getReasonString()->c_str()
                                                         : "Unknown reason" );
                    FWE_LOG_ERROR( "Server rejected subscription to topic " + mTopicName + ". Reason code " +
                                   std::to_string( reasonCode ) + ": " + reasonString );
                    subscribeFinishedPromise.set_value( false );
                    // Just return on the first error found. There could be multiple reason codes if we request multiple
                    // subscriptions at once, but we always request a single one.
                    return;
                }
            }
        }

        FWE_LOG_TRACE( "Subscribe succeeded for topic " + mTopicName + " with QoS " + grantedQoS );
        mSubscribed = true;
        subscribeFinishedPromise.set_value( true );
    };

    FWE_LOG_TRACE( "Subscribing to topic " + mTopicName );
    // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
    Aws::Crt::Mqtt5::Subscription sub1( mTopicName.c_str(), Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE );
    auto subPacket = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subPacket->WithSubscription( std::move( sub1 ) );

    if ( !mMqttClient->Subscribe( subPacket, onSubAck ) )
    {
        FWE_LOG_ERROR( "Subscribe failed" );
        return ConnectivityError::NoConnection;
    }

    // Blocked call until subscribe finished this call should quickly either fail or succeed but
    // depends on the network quality the Bootstrap needs to retry subscribing if failed.
    subscribeFinishedPromise.get_future().wait();

    if ( !mSubscribed )
    {
        return ConnectivityError::NoConnection;
    }

    return ConnectivityError::Success;
}

size_t
AwsIotChannel::getMaxSendSize() const
{
    return AWS_IOT_MAX_MESSAGE_SIZE;
}

ConnectivityError
AwsIotChannel::sendBuffer( const std::uint8_t *buf, size_t size, CollectionSchemeParams collectionSchemeParams )
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( !isTopicValid() )
    {
        FWE_LOG_WARN( "Invalid topic provided" );
        return ConnectivityError::NotConfigured;
    }

    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_WARN( "No valid data provided" );
        return ConnectivityError::WrongInputData;
    }

    if ( size > getMaxSendSize() )
    {
        FWE_LOG_WARN( "Payload provided is too long" );
        return ConnectivityError::WrongInputData;
    }

    if ( !isAliveNotThreadSafe() )
    {
        if ( mPayloadManager != nullptr )
        {
            if ( collectionSchemeParams.persist )
            {
                mPayloadManager->storeData( buf, size, collectionSchemeParams );
            }
            else
            {
                FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
            }
        }
        return ConnectivityError::NoConnection;
    }

    if ( !AwsSDKMemoryManager::getInstance().reserveMemory( size ) )
    {
        FWE_LOG_ERROR( "Not sending out the message with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory. Payload will be stored" );

        if ( collectionSchemeParams.persist )
        {
            mPayloadManager->storeData( buf, size, collectionSchemeParams );
        }
        else
        {
            FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
        }
        return ConnectivityError::QuotaReached;
    }

    publishMessage( buf, size );

    return ConnectivityError::Success;
}

ConnectivityError
AwsIotChannel::sendFile( const std::string &filePath, size_t size, CollectionSchemeParams collectionSchemeParams )
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( !isTopicValid() )
    {
        FWE_LOG_WARN( "Invalid topic provided" );
        return ConnectivityError::NotConfigured;
    }

    if ( mPayloadManager == nullptr )
    {
        FWE_LOG_WARN( "No payload manager provided" );
        return ConnectivityError::NotConfigured;
    }

    if ( filePath.empty() )
    {
        FWE_LOG_WARN( "No valid file path provided" );
        return ConnectivityError::WrongInputData;
    }

    if ( size > getMaxSendSize() )
    {
        FWE_LOG_WARN( "Payload provided is too long" );
        return ConnectivityError::WrongInputData;
    }

    if ( !isAliveNotThreadSafe() )
    {
        if ( collectionSchemeParams.persist )
        {
            // Only store metadata, file is already written on the disk
            mPayloadManager->storeMetadata( filePath, size, collectionSchemeParams );
        }
        else
        {
            FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
        }
        return ConnectivityError::NoConnection;
    }

    if ( !AwsSDKMemoryManager::getInstance().reserveMemory( size ) )
    {
        FWE_LOG_ERROR( "Not sending out the message with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory" );
        {
            if ( collectionSchemeParams.persist )
            {
                // Only store metadata, file is already written on the disk
                mPayloadManager->storeMetadata( filePath, size, collectionSchemeParams );
            }
            else
            {
                FWE_LOG_TRACE( "CollectionScheme does not activate persistency on disk" );
            }
        }
        return ConnectivityError::QuotaReached;
    }

    std::vector<uint8_t> payload( size );
    if ( mPayloadManager->retrievePayload( payload.data(), payload.size(), filePath ) != ErrorCode::SUCCESS )
    {
        return ConnectivityError::WrongInputData;
    }

    publishMessage( payload.data(), payload.size() );

    return ConnectivityError::Success;
}

void
AwsIotChannel::publishMessage( const uint8_t *buf, size_t size )
{
    auto payload = Aws::Crt::ByteBufFromArray( buf, size );

    auto onPublishComplete = [size, this]( int errorCode,
                                           std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> result ) mutable {
        {
            std::lock_guard<std::mutex> connectivityLambdaLock( mConnectivityLambdaMutex );
            AwsSDKMemoryManager::getInstance().releaseReservedMemory( size );
        }

        if ( result->wasSuccessful() )
        {
            FWE_LOG_TRACE( "Publish succeeded" );
            mPayloadCountSent++;
        }
        else
        {
            auto errorString = Aws::Crt::ErrorDebugString( errorCode );
            FWE_LOG_ERROR( std::string( "Operation failed with error" ) +
                           ( errorString != nullptr ? std::string( errorString ) : std::string( "Unknown error" ) ) );
        }
    };
    std::shared_ptr<Aws::Crt::Mqtt5::PublishPacket> publishPacket =
        std::make_shared<Aws::Crt::Mqtt5::PublishPacket>( mTopicName.c_str(),
                                                          Aws::Crt::ByteCursorFromByteBuf( payload ),
                                                          Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE );
    mMqttClient->Publish( publishPacket, onPublishComplete );
}

bool
AwsIotChannel::unsubscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( isAliveNotThreadSafe() )
    {
        std::promise<bool> unsubscribeFinishedPromise;
        FWE_LOG_TRACE( "Unsubscribing..." );
        auto unsubPacket = std::make_shared<Aws::Crt::Mqtt5::UnsubscribePacket>();
        // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
        unsubPacket->WithTopicFilter( mTopicName.c_str() );
        mMqttClient->Unsubscribe( unsubPacket, [&]( int errorCode, std::shared_ptr<UnSubAckPacket> unsubAckPacket ) {
            if ( errorCode != 0 )
            {
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
                auto errorString = Aws::Crt::ErrorDebugString( errorCode );
                FWE_LOG_ERROR( "Unsubscribe failed with error code " + std::to_string( errorCode ) + ": " +
                               std::string( errorString != nullptr ? errorString : "Unknown error" ) );
                unsubscribeFinishedPromise.set_value( false );
                return;
            }

            if ( unsubAckPacket != nullptr )
            {
                for ( Aws::Crt::Mqtt5::UnSubAckReasonCode reasonCode : unsubAckPacket->getReasonCodes() )
                {
                    if ( reasonCode >= Aws::Crt::Mqtt5::UnSubAckReasonCode::AWS_MQTT5_UARC_UNSPECIFIED_ERROR )
                    {
                        // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
                        auto reasonString = std::string( unsubAckPacket->getReasonString().has_value()
                                                             ? unsubAckPacket->getReasonString()->c_str()
                                                             : "Unknown reason" );
                        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
                        FWE_LOG_ERROR( "Server rejected unsubscribe from topic " + mTopicName + ". Reason code " +
                                       std::to_string( reasonCode ) + ": " + reasonString );
                        unsubscribeFinishedPromise.set_value( false );
                        // Just return on the first error found. There could be multiple reason codes if we unsubscribe
                        // from multiple subscriptions at once, but we always request a single one.
                        return;
                    }
                }
            }
            FWE_LOG_TRACE( "Unsubscribed from topic " + mTopicName );
            mSubscribed = false;
            unsubscribeFinishedPromise.set_value( true );
        } );
        // Blocked call until subscribe finished this call should quickly either fail or succeed but
        // depends on the network quality the Bootstrap needs to retry subscribing if failed.
        unsubscribeFinishedPromise.get_future().wait();
        return true;
    }
    return false;
}

AwsIotChannel::~AwsIotChannel()
{
    unsubscribe();
}

} // namespace IoTFleetWise
} // namespace Aws
