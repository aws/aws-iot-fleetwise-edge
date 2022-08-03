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

#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
/**
 * @brief Class to allow for CollectionSchemeManager to trigger callbacks on the Schema Class
 */
class SchemaListener
{
public:
    virtual ~SchemaListener() = default;

    /**
     * @brief Callback implementation for receiving sendCheckin requests from CollectionScheme Management
     *
     * @param documentARNs A list containing loaded collectionScheme ID's in the form of ARNs (both active and inactive)
     * and the loaded Decoder Manifest ID in the form of ARN if one is present. Documents do not need to be in any
     * order.
     * @return True if the message has been sent. False otherwise.
     */
    virtual bool sendCheckin( const std::vector<std::string> &documentARNs ) = 0;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
