// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/AwsBootstrap.h"
#include "aws/iotfleetwise/AwsGreengrassCoreIpcClientWrapper.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "gmock/gmock.h"
#include <aws/crt/Allocator.h>
#include <aws/crt/Types.h>
#include <aws/eventstreamrpc/EventStreamClient.h>
#include <aws/greengrass/GreengrassCoreIpcModel.h>
#include <boost/variant.hpp>
#include <boost/variant/variant.hpp>
#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

struct OperationResponse
{
    RpcError activateResponse;
    boost::variant<std::shared_ptr<AbstractShapeBase>, std::shared_ptr<OperationError>, RpcError> getResultResponse;
};

class SubscribeToIoTCoreOperationWrapperMock : public SubscribeToIoTCoreOperationWrapper
{
public:
    SubscribeToIoTCoreOperationWrapperMock(
        const std::unordered_map<std::string, std::unique_ptr<OperationResponse>> &topicNameToSubscribeResponses,
        std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler> streamHandler,
        std::unordered_map<std::string, std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler>>
            &subscribedTopicToHandler )
        : SubscribeToIoTCoreOperationWrapper( nullptr )
        , mTopicNameToSubscribeResponses( topicNameToSubscribeResponses )
        , mStreamHandler( std::move( streamHandler ) )
        , mSubscribedTopicToHandler( subscribedTopicToHandler )
    {
    }

    std::future<RpcError>
    Activate( const Greengrass::SubscribeToIoTCoreRequest &request,
              OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept override
    {
        static_cast<void>( onMessageFlushCallback );

        Crt::JsonObject payloadObject;
        request.SerializeToJsonObject( payloadObject );
        mTopicName = std::string( payloadObject.View().GetString( "topicName" ).c_str() );
        auto response = mTopicNameToSubscribeResponses.find( mTopicName );
        if ( response != mTopicNameToSubscribeResponses.end() )
        {
            FWE_LOG_INFO( "Returning predefined Activate response for topic: " + mTopicName );
            mActivatePromise.set_value( response->second->activateResponse );
        }
        else
        {
            FWE_LOG_ERROR( "No Activate response found for topic: " + mTopicName );
            mActivatePromise.set_value( RpcError{ EVENT_STREAM_RPC_UNMAPPED_DATA, 1 } );
        }

        return mActivatePromise.get_future();
    }

    std::future<Greengrass::SubscribeToIoTCoreResult>
    GetResult() noexcept override
    {
        auto response = mTopicNameToSubscribeResponses.find( mTopicName );
        if ( response != mTopicNameToSubscribeResponses.end() )
        {
            FWE_LOG_INFO( "Returning predefined GetResult response for topic: " + mTopicName );
            auto getResultResponse = response->second->getResultResponse;
            if ( getResultResponse.type() == typeid( std::shared_ptr<AbstractShapeBase> ) )
            {
                FWE_LOG_ERROR( "GetResult response for topic: " + mTopicName + " is an Operation response" );
                mSubscribedTopicToHandler[mTopicName] = mStreamHandler;

                auto operationResponse = boost::get<std::shared_ptr<AbstractShapeBase>>( getResultResponse );
                mGetResultPromise.set_value( Greengrass::SubscribeToIoTCoreResult( TaggedResult(
                    Crt::ScopedResource<AbstractShapeBase>( operationResponse.get(),
                                                            // Intentionally set the deleter to be a dummy function,
                                                            // because we can't copy the operationResponse
                                                            []( AbstractShapeBase * ) {} ) ) ) );
            }
            else if ( getResultResponse.type() == typeid( std::shared_ptr<OperationError> ) )
            {
                FWE_LOG_ERROR( "GetResult response for topic: " + mTopicName + " is an Operation error" );
                auto operationError = boost::get<std::shared_ptr<OperationError>>( getResultResponse );
                mGetResultPromise.set_value( Greengrass::SubscribeToIoTCoreResult( TaggedResult(
                    Crt::ScopedResource<OperationError>( operationError.get(),
                                                         // Intentionally set the deleter to be a dummy function,
                                                         // because we can't copy the operationError
                                                         []( OperationError * ) {} ) ) ) );
            }
            else if ( getResultResponse.type() == typeid( RpcError ) )
            {
                FWE_LOG_ERROR( "GetResult response for topic: " + mTopicName + " is an RpcError" );
                auto rpcError = boost::get<RpcError>( getResultResponse );
                if ( rpcError.baseStatus == EVENT_STREAM_RPC_SUCCESS )
                {
                    mSubscribedTopicToHandler[mTopicName] = mStreamHandler;
                }
                mGetResultPromise.set_value( Greengrass::SubscribeToIoTCoreResult( TaggedResult( rpcError ) ) );
            }
        }
        else
        {
            FWE_LOG_ERROR( "No GetResult response found for topic: " + mTopicName );
            mGetResultPromise.set_value(
                Greengrass::SubscribeToIoTCoreResult( TaggedResult( RpcError{ EVENT_STREAM_RPC_UNMAPPED_DATA, 1 } ) ) );
        }

        return mGetResultPromise.get_future();
    }

    std::future<RpcError>
    Close( OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept override
    {
        static_cast<void>( onMessageFlushCallback );

        FWE_LOG_INFO( "Closing subscription to topic: " + mTopicName );

        // Remove the handler mapping to indicate the topic is unsubscribed from and to prevent
        // messages being published to it.
        mSubscribedTopicToHandler.erase( mTopicName );

        mClosePromise.set_value( RpcError{ EVENT_STREAM_RPC_SUCCESS, 0 } );
        return mClosePromise.get_future();
    }

private:
    const std::unordered_map<std::string, std::unique_ptr<OperationResponse>> &mTopicNameToSubscribeResponses;
    std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler> mStreamHandler;
    std::unordered_map<std::string, std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler>>
        &mSubscribedTopicToHandler;
    std::string mTopicName;
    std::promise<RpcError> mActivatePromise;
    std::promise<Greengrass::SubscribeToIoTCoreResult> mGetResultPromise;
    std::promise<RpcError> mClosePromise;
};

class PublishToIoTCoreOperationWrapperMock : public PublishToIoTCoreOperationWrapper
{
public:
    PublishToIoTCoreOperationWrapperMock(
        const std::unordered_map<std::string, std::unique_ptr<OperationResponse>> &topicNameToPublishResponses,
        std::unordered_map<std::string, std::vector<std::string>> &topicNameToPublishedData )
        : PublishToIoTCoreOperationWrapper( nullptr )
        , mTopicNameToPublishResponses( topicNameToPublishResponses )
        , mTopicNameToPublishedData( topicNameToPublishedData )
    {
    }

    std::future<RpcError>
    Activate( const Greengrass::PublishToIoTCoreRequest &request,
              OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept override
    {
        static_cast<void>( onMessageFlushCallback );

        Crt::JsonObject payloadObject;
        request.SerializeToJsonObject( payloadObject );
        mTopicName = std::string( payloadObject.View().GetString( "topicName" ).c_str() );
        auto response = mTopicNameToPublishResponses.find( mTopicName );
        if ( response != mTopicNameToPublishResponses.end() )
        {
            FWE_LOG_INFO( "Returning predefined Activate response for topic: " + mTopicName );
            mActivatePromise.set_value( response->second->activateResponse );
            auto decodedPayload = Crt::Base64Decode( payloadObject.View().GetString( "payload" ) );
            mPublishedData.emplace_back( decodedPayload.begin(), decodedPayload.end() );
        }
        else
        {
            FWE_LOG_ERROR( "No Activate response found for topic: " + mTopicName );
            mActivatePromise.set_value( RpcError{ EVENT_STREAM_RPC_UNMAPPED_DATA, 1 } );
        }

        return mActivatePromise.get_future();
    }

    std::future<Greengrass::PublishToIoTCoreResult>
    GetResult() noexcept override
    {
        auto response = mTopicNameToPublishResponses.find( mTopicName );
        if ( response != mTopicNameToPublishResponses.end() )
        {
            FWE_LOG_INFO( "Returning predefined GetResult response for topic: " + mTopicName );
            auto getResultResponse = response->second->getResultResponse;
            if ( getResultResponse.type() == typeid( std::shared_ptr<AbstractShapeBase> ) )
            {
                FWE_LOG_ERROR( "GetResult response for topic: " + mTopicName + " is an Operation response" );
                std::copy( mPublishedData.begin(),
                           mPublishedData.end(),
                           std::back_inserter( mTopicNameToPublishedData[mTopicName] ) );
                mPublishedData.clear();

                auto operationResponse = boost::get<std::shared_ptr<AbstractShapeBase>>( getResultResponse );
                mGetResultPromise.set_value( Greengrass::PublishToIoTCoreResult( TaggedResult(
                    Crt::ScopedResource<AbstractShapeBase>( operationResponse.get(),
                                                            // Intentionally set the deleter to be a dummy function,
                                                            // because we can't copy the operationResponse
                                                            []( AbstractShapeBase * ) {} ) ) ) );
            }
            else if ( getResultResponse.type() == typeid( std::shared_ptr<OperationError> ) )
            {
                FWE_LOG_ERROR( "GetResult response for topic: " + mTopicName + " is an Operation error" );
                auto operationError = boost::get<std::shared_ptr<OperationError>>( getResultResponse );
                mGetResultPromise.set_value( Greengrass::PublishToIoTCoreResult( TaggedResult(
                    Crt::ScopedResource<OperationError>( operationError.get(),
                                                         // Intentionally set the deleter to be a dummy function,
                                                         // because we can't copy the operationError
                                                         []( OperationError * ) {} ) ) ) );
            }
            else if ( getResultResponse.type() == typeid( RpcError ) )
            {
                FWE_LOG_ERROR( "GetResult response for topic: " + mTopicName + " is an RpcError" );
                auto rpcError = boost::get<RpcError>( getResultResponse );
                if ( rpcError.baseStatus == EVENT_STREAM_RPC_SUCCESS )
                {
                    std::copy( mPublishedData.begin(),
                               mPublishedData.end(),
                               std::back_inserter( mTopicNameToPublishedData[mTopicName] ) );
                    mPublishedData.clear();
                }
                mGetResultPromise.set_value( Greengrass::PublishToIoTCoreResult( TaggedResult( rpcError ) ) );
            }
        }
        else
        {
            FWE_LOG_ERROR( "No GetResult response found for topic: " + mTopicName );
            mGetResultPromise.set_value(
                Greengrass::PublishToIoTCoreResult( TaggedResult( RpcError{ EVENT_STREAM_RPC_UNMAPPED_DATA, 1 } ) ) );
        }

        return mGetResultPromise.get_future();
    }

    std::future<RpcError>
    Close( OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept override
    {
        static_cast<void>( onMessageFlushCallback );

        FWE_LOG_INFO( "Closing publish operation to topic: " + mTopicName );
        mClosePromise.set_value( RpcError{ EVENT_STREAM_RPC_SUCCESS, 0 } );
        return mClosePromise.get_future();
    }

private:
    const std::unordered_map<std::string, std::unique_ptr<OperationResponse>> &mTopicNameToPublishResponses;
    std::unordered_map<std::string, std::vector<std::string>> &mTopicNameToPublishedData;
    std::vector<std::string> mPublishedData;
    std::string mTopicName;
    std::promise<RpcError> mActivatePromise;
    std::promise<Greengrass::PublishToIoTCoreResult> mGetResultPromise;
    std::promise<RpcError> mClosePromise;
};

class AwsGreengrassCoreIpcClientWrapperMock : public AwsGreengrassCoreIpcClientWrapper
{
public:
    AwsGreengrassCoreIpcClientWrapperMock()
        : AwsGreengrassCoreIpcClientWrapper( nullptr )
    {
        // We need to ensure the SDK bootstrap is initialized otherwise allocations in the CRT
        // library could fail.
        AwsBootstrap::getInstance().getClientBootStrap();
    };

    MOCK_METHOD( std::future<RpcError>,
                 Connect,
                 ( ConnectionLifecycleHandler & lifecycleHandler, const ConnectionConfig &connectionConfig ),
                 ( noexcept, override ) );

    MOCK_METHOD( bool, IsConnected, (), ( const, noexcept, override ) );

    MOCK_METHOD( void, Close, (), ( noexcept, override ) );

    MOCK_METHOD( void, WithLaunchMode, ( std::launch mode ), ( noexcept, override ) );

    std::shared_ptr<SubscribeToIoTCoreOperationWrapper>
    NewSubscribeToIoTCore(
        std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler> streamHandler ) noexcept override
    {
        // Mocking using MOCK_METHOD would be complicated as the Subscribe operation involves creating
        // many different objects. So to simplify, the Mock does all the work of tracking which
        // topics are subscribed and the test can just call setSubscribeResponse, publishToSubscribedTopic
        // and isTopicSubscribed instead of setting complicated expectations.
        auto operation = std::make_shared<SubscribeToIoTCoreOperationWrapperMock>(
            mTopicNameToSubscribeResponses, streamHandler, mSubscribedTopicToHandler );
        return operation;
    }

    void
    setSubscribeResponse( std::string topicName, std::unique_ptr<OperationResponse> subscribeResponse )
    {
        mTopicNameToSubscribeResponses[topicName] = std::move( subscribeResponse );
    }

    bool
    publishToSubscribedTopic( const std::string &topicName, const std::string &data )
    {
        auto subscribedTopicAndHandler = mSubscribedTopicToHandler.find( topicName );
        if ( subscribedTopicAndHandler == mSubscribedTopicToHandler.end() )
        {
            FWE_LOG_ERROR( "No handler found for topic: " + topicName );
            return false;
        }

        Greengrass::MQTTMessage mqttMessage;
        mqttMessage.SetTopicName( topicName.c_str() );
        mqttMessage.SetPayload( Crt::Vector<uint8_t>( data.begin(), data.end() ) );
        Greengrass::IoTCoreMessage iotCoreMessage;
        iotCoreMessage.SetMessage( mqttMessage );

        subscribedTopicAndHandler->second->OnStreamEvent( &iotCoreMessage );
        return true;
    }

    bool
    isTopicSubscribed( const std::string &topicName )
    {
        return mSubscribedTopicToHandler.find( topicName ) != mSubscribedTopicToHandler.end();
    }

    MOCK_METHOD( std::shared_ptr<Greengrass::ResumeComponentOperation>,
                 NewResumeComponent,
                 (),
                 ( noexcept, override ) );

    std::shared_ptr<PublishToIoTCoreOperationWrapper>
    NewPublishToIoTCore() noexcept override
    {
        // Mocking using MOCK_METHOD would be complicated as the Subscribe operation involves creating
        // many different objects. So to simplify, the Mock does all the work of tracking which
        // topics are being published and the test can just call setPublishResponse and getPublishedData
        // instead of setting complicated expectations.
        auto operation = std::make_shared<PublishToIoTCoreOperationWrapperMock>( mTopicNameToPublishResponses,
                                                                                 mTopicNameToPublishedData );
        return operation;
    }

    void
    setPublishResponse( std::string topicName, std::unique_ptr<OperationResponse> publishResponse )
    {
        mTopicNameToPublishResponses[topicName] = std::move( publishResponse );
    }

    const std::unordered_map<std::string, std::vector<std::string>> &
    getPublishedData()
    {
        return mTopicNameToPublishedData;
    }

    MOCK_METHOD( std::shared_ptr<Greengrass::SubscribeToConfigurationUpdateOperation>,
                 NewSubscribeToConfigurationUpdate,
                 ( std::shared_ptr<Greengrass::SubscribeToConfigurationUpdateStreamHandler> streamHandler ),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::DeleteThingShadowOperation>,
                 NewDeleteThingShadow,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::PutComponentMetricOperation>,
                 NewPutComponentMetric,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::DeferComponentUpdateOperation>,
                 NewDeferComponentUpdate,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::SubscribeToValidateConfigurationUpdatesOperation>,
                 NewSubscribeToValidateConfigurationUpdates,
                 ( std::shared_ptr<Greengrass::SubscribeToValidateConfigurationUpdatesStreamHandler> streamHandler ),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::GetConfigurationOperation>,
                 NewGetConfiguration,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::SubscribeToTopicOperation>,
                 NewSubscribeToTopic,
                 ( std::shared_ptr<Greengrass::SubscribeToTopicStreamHandler> streamHandler ),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::GetComponentDetailsOperation>,
                 NewGetComponentDetails,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::GetClientDeviceAuthTokenOperation>,
                 NewGetClientDeviceAuthToken,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::PublishToTopicOperation>, NewPublishToTopic, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::SubscribeToCertificateUpdatesOperation>,
                 NewSubscribeToCertificateUpdates,
                 ( std::shared_ptr<Greengrass::SubscribeToCertificateUpdatesStreamHandler> streamHandler ),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::VerifyClientDeviceIdentityOperation>,
                 NewVerifyClientDeviceIdentity,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::AuthorizeClientDeviceActionOperation>,
                 NewAuthorizeClientDeviceAction,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::ListComponentsOperation>, NewListComponents, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::CreateDebugPasswordOperation>,
                 NewCreateDebugPassword,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::GetThingShadowOperation>, NewGetThingShadow, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::SendConfigurationValidityReportOperation>,
                 NewSendConfigurationValidityReport,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::UpdateThingShadowOperation>,
                 NewUpdateThingShadow,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::UpdateConfigurationOperation>,
                 NewUpdateConfiguration,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::ValidateAuthorizationTokenOperation>,
                 NewValidateAuthorizationToken,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::RestartComponentOperation>,
                 NewRestartComponent,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::GetLocalDeploymentStatusOperation>,
                 NewGetLocalDeploymentStatus,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::GetSecretValueOperation>, NewGetSecretValue, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::UpdateStateOperation>, NewUpdateState, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::CancelLocalDeploymentOperation>,
                 NewCancelLocalDeployment,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::ListNamedShadowsForThingOperation>,
                 NewListNamedShadowsForThing,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::SubscribeToComponentUpdatesOperation>,
                 NewSubscribeToComponentUpdates,
                 ( std::shared_ptr<Greengrass::SubscribeToComponentUpdatesStreamHandler> streamHandler ),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::ListLocalDeploymentsOperation>,
                 NewListLocalDeployments,
                 (),
                 ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::StopComponentOperation>, NewStopComponent, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::PauseComponentOperation>, NewPauseComponent, (), ( noexcept, override ) );

    MOCK_METHOD( std::shared_ptr<Greengrass::CreateLocalDeploymentOperation>,
                 NewCreateLocalDeployment,
                 (),
                 ( noexcept, override ) );

private:
    std::unordered_map<std::string, std::unique_ptr<OperationResponse>> mTopicNameToSubscribeResponses;
    std::unordered_map<std::string, std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler>>
        mSubscribedTopicToHandler;

    std::unordered_map<std::string, std::unique_ptr<OperationResponse>> mTopicNameToPublishResponses;
    std::unordered_map<std::string, std::vector<std::string>> mTopicNameToPublishedData;
};

} // namespace IoTFleetWise
} // namespace Aws
