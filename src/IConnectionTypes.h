// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace Aws
{
namespace IoTFleetWise
{

/**
 *  @brief Defines the return code used by the Connectivity API
 */
enum class ConnectivityError
{
    Success = 0,      /**< everything OK, still no guarantee that data was transmitted correctly */
    NoConnection,     /**< currently no connection, the Connectivity module will try to reestablish it automatically */
    QuotaReached,     /**< quota reached for example outgoing queue full so please try again after few milliseconds */
    NotConfigured,    /**< the object used was not  configured correctly */
    WrongInputData,   /**< invalid input data was provided */
    TypeNotSupported, /**< requested upload type is not supported by the sender */
};

} // namespace IoTFleetWise
} // namespace Aws
