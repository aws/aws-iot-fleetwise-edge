// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsGGConnectivityModule.h"
#include "AwsGGChannel.h"
#include "LoggingModule.h"
#include "TraceModule.h"

#include <algorithm>
#include <aws/crt/io/HostResolver.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>

#include "Thread.h"

using namespace Aws::Crt;
using namespace Aws::Greengrass;
using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;

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
    mConnection = std::make_shared<GreengrassCoreIpcClient>( *( mClientBootstrap ) );
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
