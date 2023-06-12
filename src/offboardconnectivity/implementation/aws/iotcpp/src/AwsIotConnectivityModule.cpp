// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "AwsBootstrap.h"
#include "AwsSDKMemoryManager.h"
#include "LoggingModule.h"
#include "TraceModule.h"

#include <algorithm>
#include <aws/crt/UUID.h>
#include <iostream>
#include <mutex>
#include <sstream>

#include "Thread.h"
#include <cstdio>

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
    : mRetryThread( *this, RETRY_FIRST_CONNECTION_START_BACKOFF_MS, RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS )
    , mConnected( false )
    , mConnectionEstablished( false )
{
}

std::size_t
AwsIotConnectivityModule::reserveMemoryUsage( std::size_t bytes )
{
    return AwsSDKMemoryManager::getInstance().reserveMemory( bytes );
}

std::size_t
AwsIotConnectivityModule::releaseMemoryUsage( std::size_t bytes )
{
    return AwsSDKMemoryManager::getInstance().releaseReservedMemory( bytes );
}

/*
 * As first step to enable backend communication this code is mainly oriented on the basic_pub_sub
 * example from the Aws Iot C++ SDK
 */
bool
AwsIotConnectivityModule::connect( const std::string &privateKey,
                                   const std::string &certificate,
                                   const std::string &rootCA,
                                   const std::string &endpointUrl,
                                   const std::string &clientId,
                                   Aws::Crt::Io::ClientBootstrap *clientBootstrap,
                                   bool asynchronous )
{
    mClientId = clientId.c_str() != nullptr ? clientId.c_str() : "";

    mConnected = false;
    mCertificate = Crt::ByteCursorFromCString( certificate.c_str() );
    mEndpointUrl = endpointUrl.c_str() != nullptr ? endpointUrl.c_str() : "";
    mPrivateKey = Crt::ByteCursorFromCString( privateKey.c_str() );
    mRootCA = Crt::ByteCursorFromCString( rootCA.c_str() );

    if ( !createMqttConnection( clientBootstrap ) )
    {
        return false;
    }

    FWE_LOG_INFO( "Establishing an MQTT Connection" );
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
                                            std::size_t maximumIotSDKHeapMemoryBytes )
{
    auto channel = std::make_shared<AwsIotChannel>( this, payloadManager, maximumIotSDKHeapMemoryBytes );
    mChannels.emplace_back( channel );
    return channel;
}

bool
AwsIotConnectivityModule::resetConnection()
{
    mConnectionCompletedPromise = std::promise<bool>();
    if ( mConnectionEstablished )
    {
        FWE_LOG_INFO( "Closing the MQTT Connection" );
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
    for ( auto channel : mChannels )
    {
        channel->unsubscribe();
        channel->invalidateConnection();
    }
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
                auto errString = ErrorDebugString( errorCode );
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_FAILED );
                FWE_LOG_ERROR( "Connection failed with error" );
                FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
                mConnectionCompletedPromise.set_value( false );
            }
            else
            {
                if ( returnCode != Mqtt::ReturnCode::AWS_MQTT_CONNECT_ACCEPTED )
                {
                    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_REJECTED );
                    FWE_LOG_ERROR( "Connection failed with mqtt return code" );
                    FWE_LOG_ERROR( std::to_string( (int)returnCode ) );
                    mConnectionCompletedPromise.set_value( false );
                }
                else
                {
                    FWE_LOG_INFO( "Connection completed successfully" );
                    mConnectionCompletedPromise.set_value( true );
                }
            }
            renameEventLoopTask();
        };

    auto onInterrupted = [&]( Mqtt::MqttConnection &mqttConnection, int error ) {
        (void)mqttConnection;
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_INTERRUPTED );
        std::string errorString = "The MQTT Connection has been interrupted due to: ";
        auto errStr = ErrorDebugString( error );
        errorString.append( errStr != nullptr ? std::string( errStr ) : std::string( "Unknown error" ) );
        FWE_LOG_ERROR( errorString );
        mConnected = false;
    };

    auto onResumed = [&]( Mqtt::MqttConnection &mqttConnection, Mqtt::ReturnCode connectCode, bool sessionPresent ) {
        (void)mqttConnection;
        (void)connectCode;
        (void)sessionPresent;
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_RESUMED );
        FWE_LOG_INFO( "The MQTT Connection has resumed" );
        mConnected = true;
    };

    auto onDisconnect = [&]( Mqtt::MqttConnection &mqttConnection ) {
        (void)mqttConnection;
        FWE_LOG_INFO( "The MQTT Connection is closed" );
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
        FWE_LOG_TRACE( os.str() );
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
    Aws::IoTFleetWise::Platform::Linux::Thread::SetCurrentThreadName( "fwCNConnectMod" );
}

bool
AwsIotConnectivityModule::createMqttConnection( Aws::Crt::Io::ClientBootstrap *clientBootstrap )
{
    if ( ( mCertificate.len == 0 ) || ( mPrivateKey.len == 0 ) || mEndpointUrl.empty() || mClientId.empty() )
    {
        FWE_LOG_ERROR( "Please provide X.509 Certificate, private Key, endpoint and client-Id" );
        return false;
    }
    if ( clientBootstrap == nullptr )
    {
        FWE_LOG_ERROR( "ClientBootstrap failed with error" );
        return false;
    }
    else if ( !( *clientBootstrap ) )
    {
        auto errString = ErrorDebugString( clientBootstrap->LastError() );
        FWE_LOG_ERROR( "ClientBootstrap failed with error" );
        FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        return false;
    }

    Aws::Iot::MqttClientConnectionConfigBuilder builder;

    builder = Aws::Iot::MqttClientConnectionConfigBuilder( mCertificate, mPrivateKey );
    if ( mRootCA.len > 0 )
    {
        builder.WithCertificateAuthority( mRootCA );
    }
    builder.WithEndpoint( mEndpointUrl );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    auto clientConfig = builder.Build();
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );

    if ( !clientConfig )
    {
        auto errString = ErrorDebugString( clientConfig.LastError() );
        FWE_LOG_ERROR( "Client Configuration initialization failed with error" );
        FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        return false;
    }
    /*
     * Documentation of MqttClient says that the parameter objects needs to live only for the
     * function call so all the needed objects like bootstrap are on the stack.
     */
    mMqttClient = std::make_unique<Aws::Iot::MqttClient>( *clientBootstrap );
    if ( !*mMqttClient )
    {
        auto errString = ErrorDebugString( mMqttClient->LastError() );
        FWE_LOG_ERROR( "MQTT Client Creation failed with error" );
        FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        return false;
    }

    mConnection = mMqttClient->NewConnection( clientConfig );

    // No call to SetReconnectTimeout so it uses the SDK reconnect defaults (currently starting at 1 second and max 128
    // seconds)
    if ( !mConnection )
    {
        auto errString = ErrorDebugString( mMqttClient->LastError() );
        auto errLog = errString != nullptr ? std::string( errString ) : std::string( "Unknown error" );
        FWE_LOG_ERROR( "MQTT Connection Creation failed with error " + errLog );
        return false;
    }
    return true;
}

RetryStatus
AwsIotConnectivityModule::attempt()
{
    if ( !mConnection->Connect( mClientId.c_str(), false, MQTT_CONNECT_KEEP_ALIVE_SECONDS, MQTT_PING_TIMOUT_MS ) )
    {
        std::string error = "The MQTT Connection failed due to: ";
        auto errString = ErrorDebugString( mConnection->LastError() );
        error.append( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        FWE_LOG_WARN( error );
        return RetryStatus::RETRY;
    }

    FWE_LOG_TRACE( "Waiting of connection completed callback" );
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
    disconnect();
}
