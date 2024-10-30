// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IConnectivityModule.h"
#include <gmock/gmock.h>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class ConnectivityModuleMock : public IConnectivityModule
{
public:
    MOCK_METHOD( bool, isAlive, (), ( const, override ) );

    std::shared_ptr<ISender>
    createSender( const std::string &topicName, QoS publishQoS = QoS::AT_MOST_ONCE ) override
    {
        return mockedCreateSender( topicName, publishQoS );
    };

    MOCK_METHOD( std::shared_ptr<ISender>, mockedCreateSender, ( const std::string &topicName, QoS publishQoS ) );

    std::shared_ptr<IReceiver>
    createReceiver( const std::string &topicName ) override
    {
        return mockedCreateReceiver( topicName );
    };

    MOCK_METHOD( std::shared_ptr<IReceiver>, mockedCreateReceiver, ( const std::string &topicName ) );

    MOCK_METHOD( bool, disconnect, (), ( override ) );

    MOCK_METHOD( bool, connect, (), ( override ) );

    MOCK_METHOD( void, subscribeToConnectionEstablished, ( OnConnectionEstablishedCallback callback ), ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
