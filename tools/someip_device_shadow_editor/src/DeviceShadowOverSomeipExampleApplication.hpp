// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "v1/commonapi/DeviceShadowOverSomeipInterfaceProxy.hpp"
#include <cstdint>
#include <memory>
#include <string>

class DeviceShadowOverSomeipExampleApplication
{
public:
    static const uint32_t DEFAULT_METHOD_TIMEOUT_MS = 5000U;

    DeviceShadowOverSomeipExampleApplication() = default;
    DeviceShadowOverSomeipExampleApplication( const DeviceShadowOverSomeipExampleApplication & ) = delete;
    DeviceShadowOverSomeipExampleApplication &operator=( const DeviceShadowOverSomeipExampleApplication & ) = delete;
    DeviceShadowOverSomeipExampleApplication( DeviceShadowOverSomeipExampleApplication && ) = delete;
    DeviceShadowOverSomeipExampleApplication &operator=( DeviceShadowOverSomeipExampleApplication && ) = delete;

    ~DeviceShadowOverSomeipExampleApplication()
    {
        deinit();
    };

    /**
     * @brief   Initializes the DeviceShadowOverSomeipExampleApplication.
     *
     * @param domain        domain (e.g. "local")
     * @param instance      instance (e.g. "commonapi.DeviceShadowOverSomeipInterface")
     * @param connection    connection (e.g. "DeviceShadowOverSomeipExampleApplication")
     */
    void init( std::string domain, std::string instance, std::string connection );

    /**
     * @brief   De-initializes the DeviceShadowOverSomeipExampleApplication.
     */
    void deinit();

    /**
     * @brief   Get the device shadow.
     *
     * @param shadowName        Name of the device shadow
     * @return Shadow document
     */
    std::string getShadow( const std::string &shadowName );

    /**
     * @brief   Update the device shadow.
     *
     * @param shadowName        Name of the device shadow
     * @param updateDocument    Shadow update request document
     * @return Shadow update response document
     */
    std::string updateShadow( const std::string &shadowName, const std::string &updateDocument );

    /**
     * @brief   Delete the device shadow.
     *
     * @param shadowName        Name of the device shadow
     */
    void deleteShadow( const std::string &shadowName );

    std::string getInstance() const;

private:
    bool mInitialized{ false };

    std::shared_ptr<v1::commonapi::DeviceShadowOverSomeipInterfaceProxy<>> mProxy;

    uint32_t mShadowChangedSubscription;
    std::string mInstance;
};
