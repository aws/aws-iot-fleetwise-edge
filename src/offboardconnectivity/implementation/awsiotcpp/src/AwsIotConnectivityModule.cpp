/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "AwsIotConnectivityModule.h"

#include "TraceModule.h"

#include <algorithm>
#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>

#include "IoTSDKCustomAllocators.h"
#include "Thread.h"
#include <stdio.h>

using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot;
using namespace Aws::Crt;
// Default MQTT Keep alive in seconds.
// Defines how often an MQTT PING message is sent to the MQTT broker to keep the connection alive
// Default set to 60 seconds. Every 60 seconds the stack will send an MQTT PING req.
// The longer this interval is, the more the stack takes to detect the state of the TCP connection
// at the lower network layers.
constexpr uint16_t MQTT_CONNECT_KEEP_ALIVE_SECONDS = 60;
// Default ping timeout value in milliseconds
// If a response is not received within this interval, the connection will be reestablished.
// If the PING request does not return within this interval, the stack will create a new one.
constexpr uint32_t MQTT_PING_TIMOUT_MS = 3000;

AwsIotConnectivityModule::AwsIotConnectivityModule()
    : mApiHandle( &gSDKAllocators )
    , mRetryThread( *this, RETRY_FIRST_CONNECTION_START_BACKOFF_MS, RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS )
    , mConnected( false )
    , mConnectionEstablished( false )
{
    /* If detailed logging of the AWS IoT SDK is needed add this line:
     * mApiHandle.InitializeLogging(LogLevel::Trace,stdout);
     * */
}
uint64_t
AwsIotConnectivityModule::getCurrentMemoryUsage()
{
    return gMemoryUsedBySDK;
}

uint64_t
AwsIotConnectivityModule::reserveMemoryUsage( uint64_t bytes )
{
    gMemoryUsedBySDK += bytes;
    return gMemoryUsedBySDK;
}
uint64_t
AwsIotConnectivityModule::releaseMemoryUsage( uint64_t bytes )
{
    gMemoryUsedBySDK -= bytes;
    return gMemoryUsedBySDK;
}

/*
 * As first step to enable backend communication this code is mainly oriented on the basic_pub_sub
 * example from the Aws Iot C++ SDK
 */
bool
AwsIotConnectivityModule::connect( const std::string &keyFilepathRef,
                                   const std::string &certificateFilepathRef,
                                   const std::string &endpointUrlRef,
                                   const std::string &clientIdRef,
                                   bool asynchronous )
{
    mClientId = clientIdRef.c_str();

    mConnected = false;
    mCertificateFilepath = certificateFilepathRef.c_str();
    mEndpointUrl = endpointUrlRef.c_str();
    mKeyFilepath = keyFilepathRef.c_str();

    if ( !createMqttConnection() )
    {
        return false;
    }

    mLogger.info( "AwsIotConnectivityModule::connect", "Establishing an MQTT Connection" );
    // Connection callbacks
    setupCallbacks();

    if ( asynchronous )
    {
        return mRetryThread.start();
    }
    else
    {
        if ( attempt() == RetryStatus::SUCCESS )
        {
            onFinished( RetryStatus::SUCCESS );
            return true;
        }
        return false;
    }
}

std::shared_ptr<AwsIotChannel>
AwsIotConnectivityModule::createNewChannel( const std::shared_ptr<PayloadManager> &payloadManager,
                                            uint64_t maximumIotSDKHeapMemoryBytes )
{
    mChannels.emplace_back( new AwsIotChannel( this, payloadManager, maximumIotSDKHeapMemoryBytes ) );
    return mChannels.back();
}

bool
AwsIotConnectivityModule::resetConnection()
{
    mConnectionCompletedPromise = std::promise<bool>();
    if ( mConnectionEstablished )
    {
        mLogger.info( "AwsIotConnectivityModule::disconnect", "Closing the MQTT Connection" );
        if ( mConnection->Disconnect() )
        {
            mConnectionClosedPromise.get_future().wait();
            mConnectionClosedPromise = std::promise<void>();
            mConnectionEstablished = false;
            return true;
        }
    }
    return false;
}

bool
AwsIotConnectivityModule::disconnect()
{
    mRetryThread.stop();
    return resetConnection();
}

void
AwsIotConnectivityModule::setupCallbacks()
{
    /*
     * This will execute when an mqtt connect has completed or failed.
     */
    auto onConnectionCompleted =
        [&]( Mqtt::MqttConnection &mqttConnection, int errorCode, Mqtt::ReturnCode returnCode, bool sessionPresent ) {
            (void)mqttConnection;
            (void)sessionPresent;
            if ( errorCode != 0 )
            {
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_FAILED );
                mLogger.error( "AwsIotConnectivityModule::connect", "Connection failed with error" );
                mLogger.error( "AwsIotConnectivityModule::connect", ErrorDebugString( errorCode ) );
                mConnectionCompletedPromise.set_value( false );
            }
            else
            {
                if ( returnCode != AWS_MQTT_CONNECT_ACCEPTED )
                {
                    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_REJECTED );
                    mLogger.error( "AwsIotConnectivityModule::connect", "Connection failed with mqtt return code" );
                    mLogger.error( "AwsIotConnectivityModule::connect", std::to_string( (int)returnCode ) );
                    mConnectionCompletedPromise.set_value( false );
                }
                else
                {
                    mLogger.info( "AwsIotConnectivityModule::connect", "Connection completed successfully." );
                    mConnectionCompletedPromise.set_value( true );
                }
            }
            renameEventLoopTask();
        };

    auto onInterrupted = [&]( Mqtt::MqttConnection &mqttConnection, int error ) {
        (void)mqttConnection;
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_INTERRUPTED );
        std::string errorString = " The MQTT Connection has been interrupted due to: ";
        errorString.append( ErrorDebugString( error ) );
        mLogger.error( "AwsIotConnectivityModule::setupCallbacks", errorString );
        mConnected = false;
    };

    auto onResumed = [&]( Mqtt::MqttConnection &mqttConnection, Mqtt::ReturnCode connectCode, bool sessionPresent ) {
        (void)mqttConnection;
        (void)connectCode;
        (void)sessionPresent;
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_RESUMED );
        mLogger.info( "AwsIotConnectivityModule::setupCallbacks", "The MQTT Connection has resumed" );
        mConnected = true;
    };

    auto onDisconnect = [&]( Mqtt::MqttConnection &mqttConnection ) {
        (void)mqttConnection;
        mLogger.info( "AwsIotConnectivityModule::setupCallbacks", "The MQTT Connection is closed" );
        mConnectionClosedPromise.set_value();
        mConnected = false;
        mConnectionEstablished = false;
    };

    mConnection->OnConnectionCompleted = std::move( onConnectionCompleted );
    mConnection->OnDisconnect = std::move( onDisconnect );
    mConnection->OnConnectionInterrupted = std::move( onInterrupted );
    mConnection->OnConnectionResumed = std::move( onResumed );

    mConnection->SetOnMessageHandler( [&]( Mqtt::MqttConnection &mqttCon,
                                           const String &topic,
                                           const ByteBuf &payload,
                                           bool dup,
                                           Mqtt::QOS qos,
                                           bool retain ) {
        std::ostringstream os;
        (void)mqttCon;
        (void)dup;
        (void)retain;
        (void)qos;
        os << "Data received on the topic:  " << topic << " with a payload length of: " << payload.len;
        mLogger.trace( "AwsIotConnectivityModule::setupCallbacks", os.str() );
    } );
}

/**
 * @brief rename current task to kConnectivity to make monitoring easier
 *
 * EventLoopGroup has a  function GetUnderlyingHandle() which member event_loops list points to
 * to struct aws_event_loop which has a member impl_data of type struct epoll_loop *.
 * There the member variable thread_created_on has the pthread id. Sadly struct epoll_loop *
 * seems to be not exposed in any header. So we can not get the pthread_id and need to fallback
 * to set the name on the first callback from the newly created thread
 * */
void
AwsIotConnectivityModule::renameEventLoopTask()
{
    Aws::IoTFleetWise::Platform::Thread::SetCurrentThreadName( "fwCNConnectMod" );
}

bool
AwsIotConnectivityModule::createMqttConnection()
{
    if ( mCertificateFilepath.empty() || mKeyFilepath.empty() || mEndpointUrl.empty() || mClientId.empty() )
    {
        mLogger.error( "AwsIotConnectivityModule::connect",
                       " Please provide path to Certificate, to private Key, endpoint and client-Id" );
        return false;
    }

    // We have currently only one connection and according to Aws Iot SDK documentation if
    // less than a few hundred connections are used only one thread should be used so parameter 1
    // is given as threadCount
    Aws::Crt::Io::EventLoopGroup eventLoopGroup( 1 );
    if ( !eventLoopGroup )
    {
        mLogger.error( "AwsIotConnectivityModule::connect", " could not create even Loop " );
        return false;
    }

    Io::DefaultHostResolver defaultHostResolver( eventLoopGroup, 1, 5 );
    Io::ClientBootstrap bootstrap( eventLoopGroup, defaultHostResolver );

    if ( !bootstrap )
    {
        mLogger.error( "AwsIotConnectivityModule::connect", " ClientBootstrap failed with error " );
        mLogger.error( "AwsIotConnectivityModule::connect", ErrorDebugString( bootstrap.LastError() ) );
        return false;
    }

    Aws::Iot::MqttClientConnectionConfigBuilder builder;

    builder = Aws::Iot::MqttClientConnectionConfigBuilder( mCertificateFilepath.c_str(), mKeyFilepath.c_str() );

    builder.WithEndpoint( mEndpointUrl );

    TraceModule::get().sectionBegin( BUILD_MQTT );
    auto clientConfig = builder.Build();
    TraceModule::get().sectionEnd( BUILD_MQTT );

    if ( !clientConfig )
    {
        mLogger.error( "AwsIotConnectivityModule::connect", "Client Configuration initialization failed with error" );
        mLogger.error( "AwsIotConnectivityModule::connect", ErrorDebugString( clientConfig.LastError() ) );
        return false;
    }
    /*
     * Documentation of MqttClient says that the parameter objects needs to live only for the
     * function call so all the needed objects like bootstrap are on the stack.
     */
    mMqttClient = std::unique_ptr<Aws::Iot::MqttClient>( new Aws::Iot::MqttClient( bootstrap ) );
    if ( !*mMqttClient )
    {
        mLogger.error( "AwsIotConnectivityModule::connect", "MQTT Client Creation failed with error" );
        mLogger.error( "AwsIotConnectivityModule::connect", ErrorDebugString( mMqttClient->LastError() ) );
        return false;
    }

    mConnection = mMqttClient->NewConnection( clientConfig );

    // No call to SetReconnectTimeout so it uses the SDK reconnect defaults (currently starting at 1 second and max 128
    // seconds)
    if ( !mConnection )
    {
        mLogger.error( "AwsIotConnectivityModule::connect",
                       "MQTT Connection Creation failed with error " +
                           std::string( ErrorDebugString( mMqttClient->LastError() ) ) );
        return false;
    }
    return true;
}

RetryStatus
AwsIotConnectivityModule::attempt()
{
    if ( !mConnection->Connect( mClientId.c_str(), false, MQTT_CONNECT_KEEP_ALIVE_SECONDS, MQTT_PING_TIMOUT_MS ) )
    {
        std::string error = " The MQTT Connection failed due to: ";
        error.append( ErrorDebugString( mConnection->LastError() ) );
        mLogger.warn( "AwsIotConnectivityModule::attempt", error );
        return RetryStatus::RETRY;
    }

    mLogger.trace( "AwsIotConnectivityModule::attempt", "Waiting of connection completed callback" );
    mConnectionEstablished = true;
    // Block until the connection establishes or fails.
    // If the connection fails, the module will also fail.
    if ( mConnectionCompletedPromise.get_future().get() )
    {
        mConnected = true;
        return RetryStatus::SUCCESS;
    }
    else
    {
        // Cleanup resources
        resetConnection();
        return RetryStatus::RETRY;
    }
}

void
AwsIotConnectivityModule::onFinished( RetryStatus code )
{
    if ( code == RetryStatus::SUCCESS )
    {
        for ( auto channel : mChannels )
        {
            if ( channel->shouldSubscribeAsynchronously() )
            {
                channel->subscribe();
            }
        }
    }
}

AwsIotConnectivityModule::~AwsIotConnectivityModule()
{
    for ( auto channel : mChannels )
    {
        channel->invalidateConnection();
    }
    disconnect();
}
