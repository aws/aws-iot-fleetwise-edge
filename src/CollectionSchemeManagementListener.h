// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICollectionSchemeList.h"
#include "IDecoderManifest.h"

namespace Aws
{
namespace IoTFleetWise
{

using IDecoderManifestPtr = std::shared_ptr<IDecoderManifest>;
using ICollectionSchemeListPtr = std::shared_ptr<ICollectionSchemeList>;

struct CollectionSchemeManagementListener
{
    virtual ~CollectionSchemeManagementListener() = default;

    /**
     * @brief Callback function that Schema uses to notify CollectionSchemeManagement when a new CollectionScheme List
     * arrives from the Cloud.
     *
     * @param collectionSchemeList ICollectionSchemeList from CollectionScheme Ingestion
     */
    virtual void onCollectionSchemeUpdate( const ICollectionSchemeListPtr &collectionSchemeList ) = 0;

    /**
     * @brief Callback function that Schema uses to notify CollectionSchemeManagement when a new Decoder
     * Manifest arrives from the Cloud.
     *
     * @param decoderManifest IDecoderManifest from CollectionScheme Ingestion
     */
    virtual void onDecoderManifestUpdate( const IDecoderManifestPtr &decoderManifest ) = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
