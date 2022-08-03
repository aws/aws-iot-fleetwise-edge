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

// Includes

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

struct CollectionSchemeListener
{
    virtual ~CollectionSchemeListener() = default;

    /**
     * @brief Invoked as soon as new collectionScheme is available
     * For now this function is only used to notify but in the future as a parameter the new
     * collectionScheme could be given  if creating the collectionScheme is moved from inside the Engine
     * thread into a separate thread
     */
    virtual void onNewCollectionSchemeAvailable() = 0;
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws