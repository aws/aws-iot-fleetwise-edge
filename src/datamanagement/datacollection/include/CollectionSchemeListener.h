// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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