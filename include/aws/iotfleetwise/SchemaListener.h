// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/SignalTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Class to allow for CollectionSchemeManager to trigger callbacks on the Schema Class
 */
class SchemaListener
{
public:
    virtual ~SchemaListener() = default;

    using OnCheckinSentCallback = std::function<void( bool success )>;

    /**
     * @brief Callback implementation for receiving sendCheckin requests from CollectionScheme Management
     *
     * @param documentARNs A list containing loaded collectionScheme ID's in the form of ARNs (both active and inactive)
     * and the loaded Decoder Manifest ID in the form of ARN if one is present. Documents do not need to be in any
     * order.
     * @param callback callback that will be called when the operation completes (successfully or not).
     *                 IMPORTANT: The callback can be called by the same thread before sendBuffer even returns
     *                 or a separate thread, depending on whether the results are known synchronously or asynchronously.
     */
    virtual void sendCheckin( const std::vector<SyncID> &documentARNs, OnCheckinSentCallback callback ) = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
