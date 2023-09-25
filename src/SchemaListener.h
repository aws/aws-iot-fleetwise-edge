// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

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

} // namespace IoTFleetWise
} // namespace Aws
