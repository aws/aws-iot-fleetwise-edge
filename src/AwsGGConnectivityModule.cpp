// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsGGConnectivityModule.h"
#include "AwsGGChannel.h"
#include "LoggingModule.h"
#include <algorithm>
#include <future>

namespace Aws
{
namespace IoTFleetWise
{

AwsGGConnectivityModule::AwsGGConnectivityModule( Aws::Crt::Io::ClientBootstrap *clientBootstrap )
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
AwsGGConnectivityModule::connect()
{

    mConnected = false;

    mLifecycleHandler = std::make_unique<IpcLifecycleHandler>();
    mConnection = std::make_shared<Aws::Greengrass::GreengrassCoreIpcClient>( *( mClientBootstrap ) );
    auto connectionStatus = mConnection->Connect( *( mLifecycleHandler.get() ) ).get();
    if ( !connectionStatus )
    {
        FWE_LOG_ERROR( "Failed to establish connection with error " + std::to_string( connectionStatus ) );
        return false;
    }

    mConnected = true;
    for ( auto channel : mChannels )
    {
        if ( channel->shouldSubscribeAsynchronously() )
        {
            channel->subscribe();
        }
    }
    FWE_LOG_INFO( "Successfully connected" );
    return true;
}

std::shared_ptr<IConnectivityChannel>
AwsGGConnectivityModule::createNewChannel( const std::shared_ptr<PayloadManager> &payloadManager )
{
    auto channel = std::make_shared<AwsGGChannel>( this, payloadManager, mConnection );
    mChannels.emplace_back( channel );
    return channel;
}

bool
AwsGGConnectivityModule::disconnect()
{
    for ( auto channel : mChannels )
    {
        channel->unsubscribe();
        channel->invalidateConnection();
    }
    return true;
}

AwsGGConnectivityModule::~AwsGGConnectivityModule()
{
    AwsGGConnectivityModule::disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws
