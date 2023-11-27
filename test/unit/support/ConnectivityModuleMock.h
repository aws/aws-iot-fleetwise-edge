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
    MOCK_METHOD( bool, isAlive, (), ( const override ) );

    std::shared_ptr<IConnectivityChannel>
    createNewChannel( const std::shared_ptr<PayloadManager> &payloadManager,
                      const std::string &topicName,
                      bool subscription = false ) override
    {
        return mockedCreateNewChannel( payloadManager, topicName, subscription );
    };

    MOCK_METHOD( std::shared_ptr<IConnectivityChannel>,
                 mockedCreateNewChannel,
                 ( const std::shared_ptr<PayloadManager> &payloadManager,
                   const std::string &topicName,
                   bool subscription ) );

    MOCK_METHOD( bool, disconnect, (), ( override ) );

    MOCK_METHOD( bool, connect, (), ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
