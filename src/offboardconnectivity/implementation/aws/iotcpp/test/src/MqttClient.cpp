// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iot/MqttClient.h"
#include "AwsIotSdkMock.h"
using namespace Aws::IoTFleetWise::OffboardConnectivityAwsIot::Testing;

Aws::Iot::MqttClient::MqttClient( Aws::Crt::Io::ClientBootstrap &bootstrap, Aws::Crt::Allocator *allocator ) noexcept
{
    getClientMock()->CONSTRUCTOR( bootstrap, allocator );
}

std::shared_ptr<Aws::Crt::Mqtt::MqttConnection>
Aws::Iot::MqttClient::NewConnection( const MqttClientConnectionConfig &config ) noexcept
{
    return getClientMock()->NewConnection( config );
}

Aws::Iot::MqttClient::operator bool() const noexcept
{
    return getClientMock()->operatorBool();
}

int
Aws::Iot::MqttClient::LastError() const noexcept
{
    return getClientMock()->LastError();
}

Aws::Iot::MqttClientConnectionConfig::operator bool() const noexcept
{
    return getConfMock()->operatorBool();
}

int
Aws::Iot::MqttClientConnectionConfig::LastError() const noexcept
{
    return getConfMock()->LastError();
}

Aws::Iot::MqttClientConnectionConfigBuilder &
Aws::Iot::MqttClientConnectionConfigBuilder::WithEndpoint( const Aws::Crt::String &endpoint )
{
    getConfBuilderMock()->WithEndpoint( endpoint );
    return *this;
}

Aws::Iot::MqttClientConnectionConfigBuilder &
Aws::Iot::MqttClientConnectionConfigBuilder::WithCertificateAuthority( const Crt::ByteCursor &rootCA )
{
    getConfBuilderMock()->WithCertificateAuthority( rootCA );
    return *this;
}

Aws::Iot::MqttClientConnectionConfig
Aws::Iot::MqttClientConnectionConfigBuilder::Build() noexcept
{
    return getConfBuilderMock()->Build();
}
