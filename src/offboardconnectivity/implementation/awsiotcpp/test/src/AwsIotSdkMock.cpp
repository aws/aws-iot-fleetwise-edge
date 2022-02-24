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

#include "AwsIotSdkMock.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivityAwsIot
{
namespace Testing
{

static AwsIotSdkMock *sdkMock = nullptr;
static MqttConnectionMock *conMock = nullptr;
static MqttClientMock *clientMock = nullptr;
static MqttClientConnectionConfigMock *confMock = nullptr;
static MqttClientConnectionConfigBuilderMock *confBuilder = nullptr;
static ClientBootstrapMock *clientBootstrap = nullptr;
static EventLoopGroupMock *eventLoopMock = nullptr;

AwsIotSdkMock *
getSdkMock()
{
    return sdkMock;
}

void
setSdkMock( AwsIotSdkMock *m )
{
    sdkMock = m;
}

MqttConnectionMock *
getConMock()
{
    return conMock;
}

void
setConMock( MqttConnectionMock *m )
{
    conMock = m;
}

MqttClientMock *
getClientMock()
{
    return clientMock;
}

void
setClientMock( MqttClientMock *m )
{
    clientMock = m;
}

MqttClientConnectionConfigMock *
getConfMock()
{
    return confMock;
}

void
setConfMock( MqttClientConnectionConfigMock *m )
{
    confMock = m;
}

MqttClientConnectionConfigBuilderMock *
getConfBuilderMock()
{
    return confBuilder;
}
void
setConfBuilderMock( MqttClientConnectionConfigBuilderMock *m )
{
    confBuilder = m;
}

ClientBootstrapMock *
getClientBootstrapMock()
{
    return clientBootstrap;
}
void
setClientBootstrapMock( ClientBootstrapMock *m )
{
    clientBootstrap = m;
}

EventLoopGroupMock *
getEventLoopMock()
{
    return eventLoopMock;
}

void
setEventLoopMock( EventLoopGroupMock *m )
{
    eventLoopMock = m;
}

} // namespace Testing
} // namespace OffboardConnectivityAwsIot
} // namespace IoTFleetWise
} // namespace Aws