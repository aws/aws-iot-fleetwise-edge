// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <aws/greengrass/GreengrassCoreIpcModel.h>
#include <aws/iot/Mqtt5Client.h>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

class SubscribeToIoTCoreOperationWrapper
{
public:
    SubscribeToIoTCoreOperationWrapper( std::shared_ptr<Greengrass::SubscribeToIoTCoreOperation> operation )
        : mOperation( std::move( operation ) )
    {
    }

    virtual std::future<RpcError>
    Activate( const Greengrass::SubscribeToIoTCoreRequest &request,
              OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept
    {
        return mOperation->Activate( request, std::move( onMessageFlushCallback ) );
    }

    virtual std::future<Greengrass::SubscribeToIoTCoreResult>
    GetResult() noexcept
    {
        return mOperation->GetResult();
    }

    virtual std::future<RpcError>
    Close( OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept
    {
        return mOperation->Close( std::move( onMessageFlushCallback ) );
    }

private:
    std::shared_ptr<Greengrass::SubscribeToIoTCoreOperation> mOperation;
};

class PublishToIoTCoreOperationWrapper
{
public:
    PublishToIoTCoreOperationWrapper( std::shared_ptr<Greengrass::PublishToIoTCoreOperation> operation )
        : mOperation( std::move( operation ) )
    {
    }

    virtual std::future<RpcError>
    Activate( const Greengrass::PublishToIoTCoreRequest &request,
              OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept
    {
        return mOperation->Activate( request, std::move( onMessageFlushCallback ) );
    }

    virtual std::future<Greengrass::PublishToIoTCoreResult>
    GetResult() noexcept
    {
        return mOperation->GetResult();
    }

    virtual std::future<RpcError>
    Close( OnMessageFlushCallback onMessageFlushCallback = nullptr ) noexcept
    {
        return mOperation->Close( std::move( onMessageFlushCallback ) );
    }

private:
    std::shared_ptr<Greengrass::PublishToIoTCoreOperation> mOperation;
};

/**
 * @brief A wrapper around GreengrassCoreIpcClient so that we can provide different implementations.
 *
 * The original GreengrassCoreIpcClient doesn't declare any virtual methods, so inheriting from it
 * wouldn't help.
 **/
class AwsGreengrassCoreIpcClientWrapper
{
public:
    /**
     * @param greengrassClient the GreengrassCoreIpcClient instance to be wrapped
     */
    AwsGreengrassCoreIpcClientWrapper( Aws::Greengrass::GreengrassCoreIpcClient *greengrassClient )
        : mGreengrassClient( greengrassClient ){};
    virtual ~AwsGreengrassCoreIpcClientWrapper() = default;

    AwsGreengrassCoreIpcClientWrapper() = delete;
    AwsGreengrassCoreIpcClientWrapper( const AwsGreengrassCoreIpcClientWrapper & ) = delete;
    AwsGreengrassCoreIpcClientWrapper &operator=( const AwsGreengrassCoreIpcClientWrapper & ) = delete;
    AwsGreengrassCoreIpcClientWrapper( AwsGreengrassCoreIpcClientWrapper && ) = delete;
    AwsGreengrassCoreIpcClientWrapper &operator=( AwsGreengrassCoreIpcClientWrapper && ) = delete;

    virtual std::future<RpcError>
    Connect( ConnectionLifecycleHandler &lifecycleHandler,
             const ConnectionConfig &connectionConfig = Greengrass::DefaultConnectionConfig() ) noexcept
    {
        return mGreengrassClient->Connect( lifecycleHandler, connectionConfig );
    }

    virtual bool
    IsConnected() const noexcept
    {
        return mGreengrassClient->IsConnected();
    }

    virtual void
    Close() noexcept
    {
        mGreengrassClient->Close();
    }

    virtual void
    WithLaunchMode( std::launch mode ) noexcept
    {
        mGreengrassClient->WithLaunchMode( mode );
    }

    virtual std::shared_ptr<SubscribeToIoTCoreOperationWrapper>
    NewSubscribeToIoTCore( std::shared_ptr<Greengrass::SubscribeToIoTCoreStreamHandler> streamHandler ) noexcept
    {
        return std::make_shared<SubscribeToIoTCoreOperationWrapper>(
            mGreengrassClient->NewSubscribeToIoTCore( std::move( streamHandler ) ) );
    }

    virtual std::shared_ptr<Greengrass::ResumeComponentOperation>
    NewResumeComponent() noexcept
    {
        return mGreengrassClient->NewResumeComponent();
    }

    virtual std::shared_ptr<PublishToIoTCoreOperationWrapper>
    NewPublishToIoTCore() noexcept
    {
        return std::make_shared<PublishToIoTCoreOperationWrapper>( mGreengrassClient->NewPublishToIoTCore() );
    }

    virtual std::shared_ptr<Greengrass::SubscribeToConfigurationUpdateOperation>
    NewSubscribeToConfigurationUpdate(
        std::shared_ptr<Greengrass::SubscribeToConfigurationUpdateStreamHandler> streamHandler ) noexcept
    {
        return mGreengrassClient->NewSubscribeToConfigurationUpdate( std::move( streamHandler ) );
    }

    virtual std::shared_ptr<Greengrass::DeleteThingShadowOperation>
    NewDeleteThingShadow() noexcept
    {
        return mGreengrassClient->NewDeleteThingShadow();
    }

    virtual std::shared_ptr<Greengrass::PutComponentMetricOperation>
    NewPutComponentMetric() noexcept
    {
        return mGreengrassClient->NewPutComponentMetric();
    }

    virtual std::shared_ptr<Greengrass::DeferComponentUpdateOperation>
    NewDeferComponentUpdate() noexcept
    {
        return mGreengrassClient->NewDeferComponentUpdate();
    }

    virtual std::shared_ptr<Greengrass::SubscribeToValidateConfigurationUpdatesOperation>
    NewSubscribeToValidateConfigurationUpdates(
        std::shared_ptr<Greengrass::SubscribeToValidateConfigurationUpdatesStreamHandler> streamHandler ) noexcept
    {
        return mGreengrassClient->NewSubscribeToValidateConfigurationUpdates( std::move( streamHandler ) );
    }
    virtual std::shared_ptr<Greengrass::GetConfigurationOperation>
    NewGetConfiguration() noexcept
    {
        return mGreengrassClient->NewGetConfiguration();
    }
    virtual std::shared_ptr<Greengrass::SubscribeToTopicOperation>
    NewSubscribeToTopic( std::shared_ptr<Greengrass::SubscribeToTopicStreamHandler> streamHandler ) noexcept
    {
        return mGreengrassClient->NewSubscribeToTopic( std::move( streamHandler ) );
    }

    virtual std::shared_ptr<Greengrass::GetComponentDetailsOperation>
    NewGetComponentDetails() noexcept
    {
        return mGreengrassClient->NewGetComponentDetails();
    }

    virtual std::shared_ptr<Greengrass::GetClientDeviceAuthTokenOperation>
    NewGetClientDeviceAuthToken() noexcept
    {
        return mGreengrassClient->NewGetClientDeviceAuthToken();
    }

    virtual std::shared_ptr<Greengrass::PublishToTopicOperation>
    NewPublishToTopic() noexcept
    {
        return mGreengrassClient->NewPublishToTopic();
    }

    virtual std::shared_ptr<Greengrass::SubscribeToCertificateUpdatesOperation>
    NewSubscribeToCertificateUpdates(
        std::shared_ptr<Greengrass::SubscribeToCertificateUpdatesStreamHandler> streamHandler ) noexcept
    {
        return mGreengrassClient->NewSubscribeToCertificateUpdates( std::move( streamHandler ) );
    }

    virtual std::shared_ptr<Greengrass::VerifyClientDeviceIdentityOperation>
    NewVerifyClientDeviceIdentity() noexcept
    {
        return mGreengrassClient->NewVerifyClientDeviceIdentity();
    }

    virtual std::shared_ptr<Greengrass::AuthorizeClientDeviceActionOperation>
    NewAuthorizeClientDeviceAction() noexcept
    {
        return mGreengrassClient->NewAuthorizeClientDeviceAction();
    }

    virtual std::shared_ptr<Greengrass::ListComponentsOperation>
    NewListComponents() noexcept
    {
        return mGreengrassClient->NewListComponents();
    }

    virtual std::shared_ptr<Greengrass::CreateDebugPasswordOperation>
    NewCreateDebugPassword() noexcept
    {
        return mGreengrassClient->NewCreateDebugPassword();
    }

    virtual std::shared_ptr<Greengrass::GetThingShadowOperation>
    NewGetThingShadow() noexcept
    {
        return mGreengrassClient->NewGetThingShadow();
    }

    virtual std::shared_ptr<Greengrass::SendConfigurationValidityReportOperation>
    NewSendConfigurationValidityReport() noexcept
    {
        return mGreengrassClient->NewSendConfigurationValidityReport();
    }

    virtual std::shared_ptr<Greengrass::UpdateThingShadowOperation>
    NewUpdateThingShadow() noexcept
    {
        return mGreengrassClient->NewUpdateThingShadow();
    }

    virtual std::shared_ptr<Greengrass::UpdateConfigurationOperation>
    NewUpdateConfiguration() noexcept
    {
        return mGreengrassClient->NewUpdateConfiguration();
    }

    virtual std::shared_ptr<Greengrass::ValidateAuthorizationTokenOperation>
    NewValidateAuthorizationToken() noexcept
    {
        return mGreengrassClient->NewValidateAuthorizationToken();
    }

    virtual std::shared_ptr<Greengrass::RestartComponentOperation>
    NewRestartComponent() noexcept
    {
        return mGreengrassClient->NewRestartComponent();
    }

    virtual std::shared_ptr<Greengrass::GetLocalDeploymentStatusOperation>
    NewGetLocalDeploymentStatus() noexcept
    {
        return mGreengrassClient->NewGetLocalDeploymentStatus();
    }

    virtual std::shared_ptr<Greengrass::GetSecretValueOperation>
    NewGetSecretValue() noexcept
    {
        return mGreengrassClient->NewGetSecretValue();
    }

    virtual std::shared_ptr<Greengrass::UpdateStateOperation>
    NewUpdateState() noexcept
    {
        return mGreengrassClient->NewUpdateState();
    }

    virtual std::shared_ptr<Greengrass::CancelLocalDeploymentOperation>
    NewCancelLocalDeployment() noexcept
    {
        return mGreengrassClient->NewCancelLocalDeployment();
    }

    virtual std::shared_ptr<Greengrass::ListNamedShadowsForThingOperation>
    NewListNamedShadowsForThing() noexcept
    {
        return mGreengrassClient->NewListNamedShadowsForThing();
    }

    virtual std::shared_ptr<Greengrass::SubscribeToComponentUpdatesOperation>
    NewSubscribeToComponentUpdates(
        std::shared_ptr<Greengrass::SubscribeToComponentUpdatesStreamHandler> streamHandler ) noexcept
    {
        return mGreengrassClient->NewSubscribeToComponentUpdates( std::move( streamHandler ) );
    }

    virtual std::shared_ptr<Greengrass::ListLocalDeploymentsOperation>
    NewListLocalDeployments() noexcept
    {
        return mGreengrassClient->NewListLocalDeployments();
    }

    virtual std::shared_ptr<Greengrass::StopComponentOperation>
    NewStopComponent() noexcept
    {
        return mGreengrassClient->NewStopComponent();
    }

    virtual std::shared_ptr<Greengrass::PauseComponentOperation>
    NewPauseComponent() noexcept
    {
        return mGreengrassClient->NewPauseComponent();
    }

    virtual std::shared_ptr<Greengrass::CreateLocalDeploymentOperation>
    NewCreateLocalDeployment() noexcept
    {
        return mGreengrassClient->NewCreateLocalDeployment();
    }

private:
    Aws::Greengrass::GreengrassCoreIpcClient *mGreengrassClient;
};

} // namespace IoTFleetWise
} // namespace Aws
