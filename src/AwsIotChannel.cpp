// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotChannel.h"
#include "AwsSDKMemoryManager.h"
#include "CacheAndPersist.h"
#include "IConnectivityModule.h"
#include "IReceiver.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <aws/common/error.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/MqttClient.h>
#include <future>
#include <sstream>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

AwsIotChannel::AwsIotChannel( IConnectivityModule *connectivityModule,
                              std::shared_ptr<PayloadManager> payloadManager,
                              std::shared_ptr<MqttConnectionWrapper> &mqttConnection )
    : mConnectivityModule( connectivityModule )
    , mPayloadManager( std::move( payloadManager ) )
    , mConnection( mqttConnection )
    , mSubscribed( false )
    , mSubscribeAsynchronously( false )
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
    return mConnectivityModule->isAlive();
}

void
AwsIotChannel::setTopic( const std::string &topicNameRef, bool subscribeAsynchronously )
{
    if ( topicNameRef.empty() )
    {
        FWE_LOG_ERROR( "Empty ingestion topic name provided" );
    }
    mSubscribeAsynchronously = subscribeAsynchronously;
    mTopicName = topicNameRef;
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
    if ( !isAliveNotThreadSafe() )
    {
        FWE_LOG_ERROR( "MQTT Connection not established, failed to subscribe" );
        return ConnectivityError::NoConnection;
    }
    /*
     * This is invoked upon the reception of a message on a subscribed topic.
     */
    auto onMessage = [&]( MqttConnectionWrapper &mqttConnection,
                          const Aws::Crt::String &topic,
                          const Aws::Crt::ByteBuf &byteBuf,
                          bool dup,
                          Aws::Crt::Mqtt::QOS qos,
                          bool retain ) {
        std::ostringstream os;
        (void)mqttConnection;
        (void)dup;
        (void)qos;
        (void)retain;
        os << "Message received on topic  " << topic << " payload length: " << byteBuf.len;
        FWE_LOG_TRACE( os.str() );
        notifyListeners<const std::uint8_t *, size_t>(
            &IReceiverCallback::onDataReceived, byteBuf.buffer, byteBuf.len );
    };

    /*
     * Subscribe for incoming publish messages on topic.
     */
    std::promise<void> subscribeFinishedPromise;
    auto onSubAck = [&]( MqttConnectionWrapper &mqttConnection,
                         uint16_t packetId,
                         const Aws::Crt::String &topic,
                         Aws::Crt::Mqtt::QOS qos,
                         int errorCode ) {
        (void)mqttConnection;
        mSubscribed = false;
        if ( errorCode != 0 )
        {
            auto errString = aws_error_debug_str( errorCode );
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
            FWE_LOG_ERROR( "Subscribe failed with error" );
            FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        }
        else
        {
            if ( ( packetId == 0U ) || ( qos == Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_FAILURE ) )
            {
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_REJECT );
                FWE_LOG_ERROR( "Subscribe rejected by the Remote broker" );
            }
            else
            {
                std::ostringstream os;
                os << "Subscribe on topic  " << topic << " on packetId " << packetId << " succeeded";
                FWE_LOG_TRACE( os.str() );
                mSubscribed = true;
            }
            subscribeFinishedPromise.set_value();
        }
    };

    FWE_LOG_TRACE( "Subscribing..." );
    mConnection->Subscribe( mTopicName.c_str(), Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage, onSubAck );

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
        FWE_LOG_ERROR( "Not sending out the message  with size " + std::to_string( size ) +
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

    auto payload = Aws::Crt::ByteBufNewCopy( Aws::Crt::DefaultAllocator(), (const uint8_t *)buf, size );

    auto onPublishComplete =
        [payload, size, this]( MqttConnectionWrapper &mqttConnection, uint16_t packetId, int errorCode ) mutable {
            /* This call means that the data was handed over to some lower level in the stack but not
                that the data is actually sent on the bus or removed from RAM*/
            (void)mqttConnection;
            aws_byte_buf_clean_up( &payload );
            {
                std::lock_guard<std::mutex> connectivityLambdaLock( mConnectivityLambdaMutex );
                AwsSDKMemoryManager::getInstance().releaseReservedMemory( size );
            }
            if ( ( packetId != 0U ) && ( errorCode == 0 ) )
            {
                FWE_LOG_TRACE( "Operation on packetId  " + std::to_string( packetId ) + " Succeeded" );
                mPayloadCountSent++;
            }
            else
            {
                auto errSting = aws_error_debug_str( errorCode );
                std::string errLog = errSting != nullptr ? std::string( errSting ) : std::string( "Unknown error" );
                FWE_LOG_ERROR( std::string( "Operation failed with error" ) + errLog );
            }
        };
    mConnection->Publish(
        mTopicName.c_str(), Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_AT_MOST_ONCE, false, payload, onPublishComplete );
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
        FWE_LOG_ERROR( "Not sending out the message  with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory. Currently allocated " );
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

    auto payloadBuffer =
        Aws::Crt::ByteBufNewCopy( Aws::Crt::DefaultAllocator(), (const uint8_t *)payload.data(), payload.size() );

    auto onPublishComplete =
        [payloadBuffer, size, this]( MqttConnectionWrapper &mqttConnection, uint16_t packetId, int errorCode ) mutable {
            /* This call means that the data was handed over to some lower level in the stack but not
                that the data is actually sent on the bus or removed from RAM */
            (void)mqttConnection;
            aws_byte_buf_clean_up( &payloadBuffer );
            {
                std::lock_guard<std::mutex> connectivityLambdaLock( mConnectivityLambdaMutex );
                AwsSDKMemoryManager::getInstance().releaseReservedMemory( size );
            }
            if ( ( packetId != 0U ) && ( errorCode == 0 ) )
            {
                FWE_LOG_TRACE( "Operation on packetId  " + std::to_string( packetId ) + " Succeeded" );
            }
            else
            {
                auto errSting = aws_error_debug_str( errorCode );
                std::string errLog = errSting != nullptr ? std::string( errSting ) : std::string( "Unknown error" );
                FWE_LOG_ERROR( std::string( "Operation failed with error" ) + errLog );
            }
        };
    mConnection->Publish(
        mTopicName.c_str(), Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_AT_MOST_ONCE, false, payloadBuffer, onPublishComplete );
    return ConnectivityError::Success;
}

bool
AwsIotChannel::unsubscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( mSubscribed && isAliveNotThreadSafe() )
    {
        std::promise<void> unsubscribeFinishedPromise;
        FWE_LOG_TRACE( "Unsubscribing..." );
        mConnection->Unsubscribe( mTopicName.c_str(),
                                  [&]( MqttConnectionWrapper &mqttConnection, uint16_t packetId, int errorCode ) {
                                      (void)mqttConnection;
                                      (void)packetId;
                                      (void)errorCode;
                                      FWE_LOG_TRACE( "Unsubscribed" );
                                      mSubscribed = false;
                                      unsubscribeFinishedPromise.set_value();
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
