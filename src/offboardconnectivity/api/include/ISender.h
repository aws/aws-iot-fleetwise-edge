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

#include "IConnectionTypes.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivity
{

/**
 * @brief Struct that specifies the persistence and transmission attributes
 *        regarding the edge to cloud payload
 */
struct CollectionSchemeParams
{
    CollectionSchemeParams()
        : persist( false )
        , compression( false )
        , priority( 0 )
    {
    }

    bool persist;      // specifies if data needs to be persisted in case of connection loss
    bool compression;  // specifies if data needs to be compressed for cloud
    uint32_t priority; // collectionScheme priority specified by the cloud
};

/**
 * @brief This interface will be used by all objects sending data to the cloud
 *
 * The configuration will done by the bootstrap with the implementing class.
 */
class ISender
{

public:
    virtual ~ISender() = default;
    /**
     * @brief indicates if the connection is established and authenticated
     *
     * The function exists only to provide some status for example for monitoring but most users of
     * of this interface do not need to call it
     * */
    virtual bool isAlive() = 0;

    /**
     * @brief get the maximum bytes that can be sent
     *
     * @return number of bytes accepted by the send function
     * */
    virtual size_t getMaxSendSize() const = 0;

    /**
     * @brief called to send data to the cloud
     *
     * The function will return fast and does not expect the parameters to outlive the
     * function call. It can be called from any thread because the function will if needed
     * copy the buffer.
     *
     * @param buf pointer to raw data to send that needs to be at least size long.
     *               The function does not care if the data is a c string, a json or a binary
     *               data stream like proto buf. The data behind buf will not be modified.
     *               The data in this buffer is associated with one collectionScheme.
     * @param size number of accessible bytes in buf. If bigger than getMaxSendSize() this function
     *              will return an error and nothing will be sent.
     * @param collectionSchemeParams object containing collectionScheme related metadata for data persistency and
     * transmission
     *
     * @return SUCCESS if connection is established.
     */
    virtual ConnectivityError send(
        const std::uint8_t *buf,
        size_t size,
        struct CollectionSchemeParams collectionSchemeParams = CollectionSchemeParams() ) = 0;
};
} // namespace OffboardConnectivity
} // namespace IoTFleetWise
} // namespace Aws