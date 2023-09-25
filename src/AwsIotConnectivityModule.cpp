// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "LoggingModule.h"
#include "Thread.h"
#include "TraceModule.h"
#include <algorithm>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/iot/MqttClient.h>
#include <sstream>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

// Default MQTT Keep alive in seconds.
// Defines how often an MQTT PING message is sent to the MQTT broker to keep the connection alive
// Default set to 60 seconds. Every 60 seconds the stack will send an MQTT PING req.
// The longer this interval is, the more the stack takes to detect the state of the TCP connection
// at the lower network layers.
// This parameter is asserted in the C SDK. It shall be strictly bigger than the default
// connection ping timeout( set to 3 seconds).
// Refer to https://github.com/awslabs/aws-c-mqtt/blob/a2ee9a321fcafa19b0473b88a54e0ae8dde5fddf/source/client.c#L1461
constexpr uint16_t MQTT_CONNECT_KEEP_ALIVE_SECONDS = 60;
// Default ping timeout value in milliseconds
// If a response is not received within this interval, the connection will be reestablished.
// If the PING request does not return within this interval, the stack will create a new one.
constexpr uint32_t MQTT_PING_TIMOUT_MS = 3000;

AwsIotConnectivityModule::AwsIotConnectivityModule(
    std::string privateKey,
    std::string certificate,
    std::string rootCA,
    std::string endpointUrl,
    std::string clientId,
    std::function<std::shared_ptr<MqttClientWrapper>()> createMqttClientWrapper,
    bool asynchronous )
    : mPrivateKey( std::move( privateKey ) )
    , mCertificate( std::move( certificate ) )
    , mRootCA( std::move( rootCA ) )
    , mEndpointUrl( std::move( endpointUrl ) )
    , mClientId( std::move( clientId ) )
    , mCreateMqttClientWrapper( std::move( createMqttClientWrapper ) )
    , mAsynchronous( asynchronous )
    , mRetryThread( *this, RETRY_FIRST_CONNECTION_START_BACKOFF_MS, RETRY_FIRST_CONNECTION_MAX_BACKOFF_MS )
    , mConnected( false )
    , mConnectionEstablished( false )
{
}

/*
 * As first step to enable backend communication this code is mainly oriented on the basic_pub_sub
 * example from the Aws Iot C++ SDK
 */
bool
AwsIotConnectivityModule::connect()
{
    mConnected = false;

    if ( !createMqttConnection() )
    {
        return false;
    }

    FWE_LOG_INFO( "Establishing an MQTT Connection" );
    // Connection callbacks
    setupCallbacks();

    if ( mAsynchronous )
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

std::shared_ptr<IConnectivityChannel>
AwsIotConnectivityModule::createNewChannel( const std::shared_ptr<PayloadManager> &payloadManager )
{
    auto channel = std::make_shared<AwsIotChannel>( this, payloadManager, mConnection );
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
    auto onConnectionCompleted = [&]( MqttConnectionWrapper &mqttConnection,
                                      int errorCode,
                                      Aws::Crt::Mqtt::ReturnCode returnCode,
                                      bool sessionPresent ) {
        (void)mqttConnection;
        (void)sessionPresent;
        if ( errorCode != 0 )
        {
            auto errString = Aws::Crt::ErrorDebugString( errorCode );
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_FAILED );
            FWE_LOG_ERROR( "Connection failed with error" );
            FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
            mConnectionCompletedPromise.set_value( false );
        }
        else
        {
            if ( returnCode != Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_ACCEPTED )
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

    auto onInterrupted = [&]( MqttConnectionWrapper &mqttConnection, int error ) {
        (void)mqttConnection;
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_INTERRUPTED );
        std::string errorString = "The MQTT Connection has been interrupted due to: ";
        auto errStr = Aws::Crt::ErrorDebugString( error );
        errorString.append( errStr != nullptr ? std::string( errStr ) : std::string( "Unknown error" ) );
        FWE_LOG_ERROR( errorString );
        mConnected = false;
    };

    auto onResumed =
        [&]( MqttConnectionWrapper &mqttConnection, Aws::Crt::Mqtt::ReturnCode connectCode, bool sessionPresent ) {
            (void)mqttConnection;
            (void)connectCode;
            (void)sessionPresent;
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::CONNECTION_RESUMED );
            FWE_LOG_INFO( "The MQTT Connection has resumed" );
            mConnected = true;
        };

    auto onDisconnect = [&]( MqttConnectionWrapper &connection ) {
        (void)connection;
        FWE_LOG_INFO( "The MQTT Connection is closed" );
        mConnectionClosedPromise.set_value();
        mConnected = false;
        mConnectionEstablished = false;
    };

    mConnection->SetOnConnectionCompleted( onConnectionCompleted );
    mConnection->SetOnDisconnect( onDisconnect );
    mConnection->SetOnConnectionInterrupted( onInterrupted );
    mConnection->SetOnConnectionResumed( onResumed );

    mConnection->SetOnMessageHandler( [&]( MqttConnectionWrapper &mqttCon,
                                           const Aws::Crt::String &topic,
                                           const Aws::Crt::ByteBuf &payload,
                                           bool dup,
                                           Aws::Crt::Mqtt::QOS qos,
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
    Thread::setCurrentThreadName( "fwCNConnectMod" );
}

bool
AwsIotConnectivityModule::createMqttConnection()
{
    if ( ( mCertificate.empty() ) || ( mPrivateKey.empty() ) || mEndpointUrl.empty() || mClientId.empty() )
    {
        FWE_LOG_ERROR( "Please provide X.509 Certificate, private Key, endpoint and client-Id" );
        return false;
    }

    auto builder = Aws::Iot::MqttClientConnectionConfigBuilder( Crt::ByteCursorFromCString( mCertificate.c_str() ),
                                                                Crt::ByteCursorFromCString( mPrivateKey.c_str() ) );
    if ( !mRootCA.empty() )
    {
        builder.WithCertificateAuthority( Crt::ByteCursorFromCString( mRootCA.c_str() ) );
    }
    builder.WithEndpoint( ( !mEndpointUrl.empty() ? mEndpointUrl.c_str() : "" ) );

    TraceModule::get().sectionBegin( TraceSection::BUILD_MQTT );
    auto clientConfig = builder.Build();
    TraceModule::get().sectionEnd( TraceSection::BUILD_MQTT );

    if ( !clientConfig )
    {
        auto errString = Aws::Crt::ErrorDebugString( clientConfig.LastError() );
        FWE_LOG_ERROR( "Client Configuration initialization failed with error" );
        FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        return false;
    }
    /*
     * Documentation of MqttClient says that the parameter objects needs to live only for the
     * function call so all the needed objects like bootstrap are on the stack.
     */
    mMqttClient = mCreateMqttClientWrapper();
    if ( !*mMqttClient )
    {
        auto errString = Aws::Crt::ErrorDebugString( mMqttClient->LastError() );
        FWE_LOG_ERROR( "MQTT Client Creation failed with error" );
        FWE_LOG_ERROR( errString != nullptr ? std::string( errString ) : std::string( "Unknown error" ) );
        return false;
    }

    mConnection = mMqttClient->NewConnection( clientConfig );

    // No call to SetReconnectTimeout so it uses the SDK reconnect defaults (currently starting at 1 second and max 128
    // seconds)
    if ( !mConnection )
    {
        auto errString = Aws::Crt::ErrorDebugString( mMqttClient->LastError() );
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
        auto errString = Aws::Crt::ErrorDebugString( mConnection->LastError() );
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
    AwsIotConnectivityModule::disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws
