// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/AwsIotReceiver.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
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

AwsIotReceiver::AwsIotReceiver( MqttClientWrapper &mqttClient, std::string topicName )
    : mMqttClient( mqttClient )
    , mTopicName( std::move( topicName ) )
    , mSubscribed( false )
{
}

AwsIotReceiver::~AwsIotReceiver()
{
    unsubscribe();
}

ConnectivityError
AwsIotReceiver::subscribe()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );
    if ( mSubscribed )
    {
        return ConnectivityError::Success;
    }

    if ( mTopicName.empty() )
    {
        FWE_LOG_ERROR( "Empty ingestion topic name provided" );
        return ConnectivityError::NotConfigured;
    }

    /*
     * Subscribe for incoming publish messages on topic.
     */
    std::promise<bool> subscribeFinishedPromise;
    auto onSubAck = [&]( int errorCode, std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> subAckPacket ) {
        mSubscribed = false;
        if ( errorCode != 0 )
        {
            auto errorString = Aws::Crt::ErrorDebugString( errorCode );
            auto logMessage = "Subscribe failed with error code " + std::to_string( errorCode ) + ": " +
                              std::string( errorString != nullptr ? errorString : "Unknown error" );
            if ( errorCode == AWS_ERROR_MQTT5_USER_REQUESTED_STOP )
            {
                FWE_LOG_TRACE( logMessage );
            }
            else
            {
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
                FWE_LOG_ERROR( logMessage );
            }
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

        FWE_LOG_INFO( "Subscribe succeeded for topic " + mTopicName + " with QoS " + grantedQoS );
        mSubscribed = true;
        subscribeFinishedPromise.set_value( true );
    };

    FWE_LOG_TRACE( "Subscribing to topic " + mTopicName );
    // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
    Aws::Crt::Mqtt5::Subscription sub1( mTopicName.c_str(), Aws::Crt::Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE );
    auto subPacket = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subPacket->WithSubscription( std::move( sub1 ) );

    if ( !mMqttClient.Subscribe( subPacket, onSubAck ) )
    {
        FWE_LOG_ERROR( "Subscribe failed for topic " + mTopicName );
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

void
AwsIotReceiver::subscribeToDataReceived( OnDataReceivedCallback callback )
{
    mListeners.subscribe( callback );
}

void
AwsIotReceiver::onDataReceived( const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData )
{
    Timestamp currentTime = mClock->monotonicTimeSinceEpochMs();
    auto mqttTopic = std::string( eventData.publishPacket->getTopic().c_str() );
    ReceivedConnectivityMessage receivedMessage{
        eventData.publishPacket->getPayload().ptr, eventData.publishPacket->getPayload().len, currentTime, mqttTopic };

    mListeners.notify( receivedMessage );
}

bool
AwsIotReceiver::unsubscribe()
{
    auto result = unsubscribeAsync();
    result.wait();
    return result.get();
}

std::future<bool>
AwsIotReceiver::unsubscribeAsync()
{
    std::lock_guard<std::mutex> connectivityLock( mConnectivityMutex );

    // We can't move the promise into the lambda, because the lambda needs to be copyable. So we
    // don't have much choice but use a shared pointer.
    auto unsubscribeFinishedPromise = std::make_shared<std::promise<bool>>();
    auto unsubscribeFuture = unsubscribeFinishedPromise->get_future();

    if ( !mSubscribed )
    {
        unsubscribeFinishedPromise->set_value( false );
        return unsubscribeFuture;
    }

    FWE_LOG_TRACE( "Unsubscribing..." );
    auto unsubPacket = std::make_shared<Aws::Crt::Mqtt5::UnsubscribePacket>();
    // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
    unsubPacket->WithTopicFilter( mTopicName.c_str() );
    mMqttClient.Unsubscribe(
        unsubPacket,
        [this, unsubscribeFinishedPromise]( int errorCode, std::shared_ptr<UnSubAckPacket> unsubAckPacket ) {
            if ( errorCode != 0 )
            {
                auto errorString = Aws::Crt::ErrorDebugString( errorCode );
                std::string logMessage = "Unsubscribe failed with error code " + std::to_string( errorCode ) + ": " +
                                         std::string( errorString != nullptr ? errorString : "Unknown error" );
                if ( errorCode == AWS_ERROR_MQTT5_USER_REQUESTED_STOP )
                {
                    FWE_LOG_TRACE( logMessage );
                }
                else
                {
                    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
                    FWE_LOG_ERROR( logMessage );
                }
                unsubscribeFinishedPromise->set_value( false );
                return;
            }

            if ( unsubAckPacket != nullptr )
            {
                for ( Aws::Crt::Mqtt5::UnSubAckReasonCode reasonCode : unsubAckPacket->getReasonCodes() )
                {
                    if ( reasonCode >= Aws::Crt::Mqtt5::UnSubAckReasonCode::AWS_MQTT5_UARC_UNSPECIFIED_ERROR )
                    {
                        // coverity[cert_str51_cpp_violation] - pointer comes from std::string, which can't be null
                        // Refer to the MQTT spec for details on the reason codes for errors.
                        // https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901179
                        auto reasonString = std::string( unsubAckPacket->getReasonString().has_value()
                                                             ? unsubAckPacket->getReasonString()->c_str()
                                                             : "Refer to the MQTT Spec for details" );
                        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::SUBSCRIBE_ERROR );
                        FWE_LOG_ERROR( "Server rejected unsubscribe from topic " + mTopicName + ". Reason code " +
                                       std::to_string( reasonCode ) + ": " + reasonString );
                        unsubscribeFinishedPromise->set_value( false );
                        // Just return on the first error found. There could be multiple reason codes if we
                        // unsubscribe from multiple subscriptions at once, but we always request a single one.
                        return;
                    }
                }
            }
            FWE_LOG_TRACE( "Unsubscribed from topic " + mTopicName );
            mSubscribed = false;
            unsubscribeFinishedPromise->set_value( true );
        } );

    return unsubscribeFuture;
}

} // namespace IoTFleetWise
} // namespace Aws
