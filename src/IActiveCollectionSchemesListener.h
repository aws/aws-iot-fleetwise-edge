// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "ICollectionSchemeList.h"

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Interface for components interested in the currently active collection schemes. Used to prepare senders based
 * on campaign data before the data is collected and selected for the upload.
 *
 */

struct ActiveCollectionSchemes
{
    std::vector<ICollectionSchemePtr> activeCollectionSchemes;
};

class IActiveCollectionSchemesListener
{
public:
    /**
     * @brief process the change of active collection schemes
     *
     * */
    virtual void onChangeCollectionSchemeList(
        const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes ) = 0;

    virtual ~IActiveCollectionSchemesListener() = default;
};

} // namespace IoTFleetWise
} // namespace Aws
