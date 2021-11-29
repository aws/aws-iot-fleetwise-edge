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

#pragma once

namespace Aws
{
namespace IoTFleetWise
{
/**
@brief OffboardConnectivity has all the classes responsible for communicating with the cloud
*/
namespace OffboardConnectivity
{

/**
 *  @brief Defines the return code used by the Connectivity API
 */
enum class ConnectivityError
{
    Success,        /**< everything OK, still no guarantee that data was transmitted correctly */
    NoConnection,   /**< currently no connection, the Connectivity module will try to reestablish it automatically */
    QuotaReached,   /**< quota reached for example outgoing queue full so please try again after few milliseconds */
    NotConfigured,  /**< the object used was not  configured correctly */
    WrongInputData, /**< Invalid input data was provided */
};

} // namespace OffboardConnectivity
} // namespace IoTFleetWise
} // namespace Aws