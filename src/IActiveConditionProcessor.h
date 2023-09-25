// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
     * @param inspectionMatrix all currently active Conditions
     * @return true if valid conditions were handed over
     * */
    virtual void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix ) = 0;

    virtual ~IActiveConditionProcessor() = default;
};

} // namespace IoTFleetWise
} // namespace Aws
