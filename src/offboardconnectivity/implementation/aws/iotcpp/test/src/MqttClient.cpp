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

Aws::Iot::MqttClientConnectionConfig
Aws::Iot::MqttClientConnectionConfigBuilder::Build() noexcept
{
    return getConfBuilderMock()->Build();
}