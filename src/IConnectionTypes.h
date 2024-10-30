// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 *  @brief Defines the return code used by the Connectivity API
 */
enum class ConnectivityError
{
    Success = 0,       /**< everything OK, still no guarantee that data was transmitted correctly */
    NoConnection,      /**< currently no connection, the Connectivity module will try to reestablish it automatically */
    QuotaReached,      /**< quota reached for example outgoing queue full so please try again after few milliseconds */
    NotConfigured,     /**< the object used was not  configured correctly */
    WrongInputData,    /**< invalid input data was provided */
    TypeNotSupported,  /**< requested upload type is not supported by the sender */
    TransmissionError, /**< some error happened after start transmission of data */
};

static inline std::string
connectivityErrorToString( ConnectivityError error )
{
    switch ( error )
    {
    case ConnectivityError::Success:
        return "Success";
    case ConnectivityError::NoConnection:
        return "NoConnection";
    case ConnectivityError::QuotaReached:
        return "QuotaReached";
    case ConnectivityError::NotConfigured:
        return "NotConfigured";
    case ConnectivityError::WrongInputData:
        return "WrongInputData";
    case ConnectivityError::TypeNotSupported:
        return "TypeNotSupported";
    case ConnectivityError::TransmissionError:
        return "TransmissionError";
    }

    return "Undefined";
}

} // namespace IoTFleetWise
} // namespace Aws
