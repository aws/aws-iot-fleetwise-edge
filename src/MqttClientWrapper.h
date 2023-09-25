// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MqttConnectionWrapper.h"
#include <aws/iot/MqttClient.h>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief   A wrapper around MqttClient so that we can provide different implementations.
 *
 * The original MqttClient can't be inherited from because it only declares private constructors.
 **/
class MqttClientWrapper
{
public:
    /**
     * @param mqttClient the MqttClient instance to be wrapped
     */
    MqttClientWrapper( std::shared_ptr<Aws::Iot::MqttClient> mqttClient )
        : mMqttClient( std::move( mqttClient ) ){};
    virtual ~MqttClientWrapper() = default;

    MqttClientWrapper() = delete;
    MqttClientWrapper( const MqttClientWrapper & ) = delete;
    MqttClientWrapper &operator=( const MqttClientWrapper & ) = delete;
    MqttClientWrapper( MqttClientWrapper && ) = delete;
    MqttClientWrapper &operator=( MqttClientWrapper && ) = delete;

    virtual std::shared_ptr<MqttConnectionWrapper>
    NewConnection( const Aws::Iot::MqttClientConnectionConfig &config ) noexcept
    {
        return std::make_shared<MqttConnectionWrapper>( mMqttClient->NewConnection( config ) );
    }

    virtual int
    LastError() const noexcept
    {
        return mMqttClient->LastError();
    }

    virtual explicit operator bool() const noexcept
    {
        return *mMqttClient ? true : false;
    }

private:
    std::shared_ptr<Aws::Iot::MqttClient> mMqttClient;
};

} // namespace IoTFleetWise
} // namespace Aws
