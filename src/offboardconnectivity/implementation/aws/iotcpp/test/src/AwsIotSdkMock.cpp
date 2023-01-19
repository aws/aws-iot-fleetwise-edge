// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
