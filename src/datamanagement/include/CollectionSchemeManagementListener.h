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

#include "ICollectionSchemeList.h"
#include "IDecoderManifest.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using IDecoderManifestPtr = std::shared_ptr<IDecoderManifest>;
using ICollectionSchemeListPtr = std::shared_ptr<ICollectionSchemeList>;

struct CollectionSchemeManagementListener
{
    virtual ~CollectionSchemeManagementListener(){};

    /**
     * @brief Callback function that Schema uses to notify CollectionSchemeManagement when a new CollectionScheme List
     * arrives from the Cloud.
     *
     * @param collectionSchemeList ICollectionSchemeList from CollectionScheme Ingestion
     **/
    virtual void onCollectionSchemeUpdate( const ICollectionSchemeListPtr &collectionSchemeList ) = 0;

    /**
     * @brief Callback function that Schema uses to notify CollectionSchemeManagement when a new Decoder
     * Manifest arrives from the Cloud.
     *
     * @param decoderManifest IDecoderManifest from CollectionScheme Ingestion
     **/
    virtual void onDecoderManifestUpdate( const IDecoderManifestPtr &decoderManifest ) = 0;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
