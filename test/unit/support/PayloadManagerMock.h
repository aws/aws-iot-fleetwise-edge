// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadManager.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <json/json.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class PayloadManagerMock : public PayloadManager
{
public:
    PayloadManagerMock()
        : PayloadManager( nullptr ){};

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    MOCK_METHOD( bool,
                 storeData,
                 ( const std::uint8_t *buf,
                   size_t size,
                   const CollectionSchemeParams &collectionSchemeParams,
                   const struct S3UploadParams &s3UploadParams ),
                 ( override ) );
#else
    MOCK_METHOD( bool,
                 storeData,
                 ( const std::uint8_t *buf, size_t size, const CollectionSchemeParams &collectionSchemeParams ),
                 ( override ) );

#endif

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    MOCK_METHOD( void,
                 storeMetadata,
                 ( const std::string filename,
                   size_t size,
                   const CollectionSchemeParams &collectionSchemeParams,
                   const struct S3UploadParams &s3UploadParams ),
                 ( override ) );
#else
    MOCK_METHOD( void,
                 storeMetadata,
                 ( const std::string filename, size_t size, const CollectionSchemeParams &collectionSchemeParams ),
                 ( override ) );
#endif

    MOCK_METHOD( ErrorCode, retrievePayloadMetadata, ( Json::Value & files ), ( override ) );

    MOCK_METHOD( ErrorCode,
                 retrievePayload,
                 ( uint8_t * buf, size_t size, const std::string &filename ),
                 ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
