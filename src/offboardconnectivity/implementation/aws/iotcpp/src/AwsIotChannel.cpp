// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotChannel.h"
#include "AwsIotConnectivityModule.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <sstream>

using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;
using namespace Aws::IoTFleetWise::OffboardConnectivity;
using namespace Aws::Crt;

AwsIotChannel::AwsIotChannel( IConnectivityModule *connectivityModule,
                              std::shared_ptr<PayloadManager> payloadManager,
                              std::size_t maximumIotSDKHeapMemoryBytes )
    : mMaximumIotSDKHeapMemoryBytes( maximumIotSDKHeapMemoryBytes )
    , mConnectivityModule( connectivityModule )
    , mPayloadManager( std::move( payloadManager ) )
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
    auto connection = mConnectivityModule->getConnection();
    /*
     * This is invoked upon the reception of a message on a subscribed topic.
     */
    auto onMessage = [&]( Mqtt::MqttConnection &mqttConnection,
                          const String &topic,
                          const ByteBuf &byteBuf,
                          bool dup,
                          Mqtt::QOS qos,
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
    auto onSubAck = [&]( Mqtt::MqttConnection &mqttConnection,
                         uint16_t packetId,
                         const String &topic,
                         Mqtt::QOS qos,
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
            if ( ( packetId == 0U ) || ( qos == Mqtt::QOS::AWS_MQTT_QOS_FAILURE ) )
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
    connection->Subscribe( mTopicName.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage, onSubAck );

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
AwsIotChannel::sendBuffer( const std::uint8_t *buf, size_t size, struct CollectionSchemeParams collectionSchemeParams )
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
            bool isDataPersisted = mPayloadManager->storeData( buf, size, collectionSchemeParams );

            if ( isDataPersisted )
            {
                FWE_LOG_TRACE( "Payload has persisted successfully on disk" );
            }
            else
            {
                FWE_LOG_WARN( "Payload has not been persisted" );
            }
        }
        return ConnectivityError::NoConnection;
    }

    uint64_t currentMemoryUsage = mConnectivityModule->reserveMemoryUsage( size );
    if ( ( mMaximumIotSDKHeapMemoryBytes != 0 ) && ( currentMemoryUsage > mMaximumIotSDKHeapMemoryBytes ) )
    {
        mConnectivityModule->releaseMemoryUsage( size );
        FWE_LOG_ERROR( "Not sending out the message  with size " + std::to_string( size ) +
                       " because IoT device SDK allocated the maximum defined memory. Currently allocated " +
                       std::to_string( currentMemoryUsage ) );
        if ( mPayloadManager != nullptr )
        {
            bool isDataPersisted = mPayloadManager->storeData( buf, size, collectionSchemeParams );

            if ( isDataPersisted )
            {
                FWE_LOG_TRACE( "Data was persisted successfully" );
            }
            else
            {
                FWE_LOG_WARN( "Data was not persisted and is lost" );
            }
        }
        return ConnectivityError::QuotaReached;
    }

    auto connection = mConnectivityModule->getConnection();

    auto payload = ByteBufNewCopy( DefaultAllocator(), (const uint8_t *)buf, size );

    auto onPublishComplete =
        [payload, size, this]( Mqtt::MqttConnection &mqttConnection, uint16_t packetId, int errorCode ) mutable {
            /* This call means that the data was handed over to some lower level in the stack but not
                that the data is actually sent on the bus or removed from RAM*/
            (void)mqttConnection;
            aws_byte_buf_clean_up( &payload );
            {
                std::lock_guard<std::mutex> connectivityLambdaLock( mConnectivityLambdaMutex );
                if ( mConnectivityModule != nullptr )
                {
                    mConnectivityModule->releaseMemoryUsage( size );
                }
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
    connection->Publish( mTopicName.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_MOST_ONCE, false, payload, onPublishComplete );
    return ConnectivityError::Success;
}

bool
AwsIotChannel::unsubscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( mSubscribed && isAliveNotThreadSafe() )
    {
        auto connection = mConnectivityModule->getConnection();

        std::promise<void> unsubscribeFinishedPromise;
        FWE_LOG_TRACE( "Unsubscribing..." );
        connection->Unsubscribe( mTopicName.c_str(),
                                 [&]( Mqtt::MqttConnection &mqttConnection, uint16_t packetId, int errorCode ) {
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
