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

#include "CollectionInspectionAPITypes.h"
#include "ICollectionScheme.h"
#include "IDecoderManifest.h"

#include <memory>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::DataManagement;
/**
 * @brief Interface for components interested in the currently active conditions
 *
 */
class IActiveConditionProcessor
{
public:
    /**
     * @brief process the change of active conditions for example by rebuilding buffers
     *
     * This function should be called as rarely as possible.
     * All condition should fulfill the restriction like max signal id or equation depth.
     * After this call all cached signal values that were not published are deleted
     * @param activeConditions all currently active Conditions
     * @return true if valid conditions were handed over
     * */
    virtual void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &activeConditions ) = 0;

    virtual ~IActiveConditionProcessor() = default;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws