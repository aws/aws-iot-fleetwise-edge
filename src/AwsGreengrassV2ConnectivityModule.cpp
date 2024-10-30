// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsGreengrassV2ConnectivityModule.h"
#include "AwsGreengrassV2Receiver.h"
#include "AwsGreengrassV2Sender.h"
#include "LoggingModule.h"
#include <future>

namespace Aws
{
namespace IoTFleetWise
{

AwsGreengrassV2ConnectivityModule::AwsGreengrassV2ConnectivityModule( Aws::Crt::Io::ClientBootstrap *clientBootstrap )
    : mConnected( false )
    , mConnectionEstablished( false )
    , mClientBootstrap( clientBootstrap )
{
}

/*
 * As first step to enable backend communication this code is mainly oriented on the basic_pub_sub
 * example from the Aws Iot C++ SDK
 */
bool
AwsGreengrassV2ConnectivityModule::connect()
{

    mConnected = false;

    mLifecycleHandler = std::make_unique<IpcLifecycleHandler>();
    mGreengrassClient = std::make_shared<Aws::Greengrass::GreengrassCoreIpcClient>( *( mClientBootstrap ) );
    auto connectionStatus = mGreengrassClient->Connect( *( mLifecycleHandler.get() ) ).get();
    if ( !connectionStatus )
    {
        FWE_LOG_ERROR( "Failed to establish connection with error " + std::to_string( connectionStatus ) );
        return false;
    }

    mConnected = true;
    for ( auto receiver : mReceivers )
    {
        receiver->subscribe();
    }
    // subscribe to all topics first before notifying listeners for connection
    mConnectionEstablishedListeners.notify();

    FWE_LOG_INFO( "Successfully connected" );
    return true;
}

std::shared_ptr<ISender>
AwsGreengrassV2ConnectivityModule::createSender( const std::string &topicName, QoS publishQoS )
{
    auto greengrassPublishQoS = Aws::Greengrass::QOS_AT_MOST_ONCE;
    switch ( publishQoS )
    {
    case QoS::AT_MOST_ONCE:
        greengrassPublishQoS = Aws::Greengrass::QOS_AT_MOST_ONCE;
        break;
    case QoS::AT_LEAST_ONCE:
        greengrassPublishQoS = Aws::Greengrass::QOS_AT_LEAST_ONCE;
        break;
    }

    auto sender = std::make_shared<AwsGreengrassV2Sender>( this, mGreengrassClient, topicName, greengrassPublishQoS );
    mSenders.emplace_back( sender );
    return sender;
}

std::shared_ptr<IReceiver>
AwsGreengrassV2ConnectivityModule::createReceiver( const std::string &topicName )
{
    auto receiver = std::make_shared<AwsGreengrassV2Receiver>( this, mGreengrassClient, topicName );
    mReceivers.emplace_back( receiver );
    return receiver;
}

void
AwsGreengrassV2ConnectivityModule::subscribeToConnectionEstablished( OnConnectionEstablishedCallback callback )
{
    mConnectionEstablishedListeners.subscribe( callback );
}

bool
AwsGreengrassV2ConnectivityModule::disconnect()
{
    for ( auto receiver : mReceivers )
    {
        receiver->unsubscribe();
        receiver->invalidateConnection();
    }

    for ( auto sender : mSenders )
    {
        sender->invalidateConnection();
    }

    return true;
}

AwsGreengrassV2ConnectivityModule::~AwsGreengrassV2ConnectivityModule()
{
    AwsGreengrassV2ConnectivityModule::disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws