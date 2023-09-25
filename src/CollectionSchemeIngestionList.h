// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICollectionSchemeList.h"
#include "collection_schemes.pb.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Setting a size limit in bytes for incoming collectionSchemes from cloud
 */
constexpr size_t COLLECTION_SCHEME_LIST_BYTE_SIZE_LIMIT = 128000000;

class CollectionSchemeIngestionList : public ICollectionSchemeList
{
public:
    CollectionSchemeIngestionList() = default;
    ~CollectionSchemeIngestionList() override;
    CollectionSchemeIngestionList( const CollectionSchemeIngestionList & ) = delete;
    CollectionSchemeIngestionList &operator=( const CollectionSchemeIngestionList & ) = delete;
    CollectionSchemeIngestionList( CollectionSchemeIngestionList && ) = delete;
    CollectionSchemeIngestionList &operator=( CollectionSchemeIngestionList && ) = delete;

    bool isReady() const override;

    bool build() override;

    const std::vector<ICollectionSchemePtr> &getCollectionSchemes() const override;

    bool copyData( const std::uint8_t *inputBuffer, const size_t size ) override;

    inline const std::vector<uint8_t> &
    getData() const override
    {
        return mProtoBinaryData;
    }

private:
    /**
     * @brief This vector will store the binary data copied from the IReceiver callback.
     */
    std::vector<uint8_t> mProtoBinaryData;

    /**
     * @brief Flag which is true if proto binary data is processed into readable data structures.
     */
    bool mReady{ false };

    /**
     * @brief Member variable that will hold the vector of collection collectionSchemes
     */
    std::vector<ICollectionSchemePtr> mVectorCollectionSchemePtr;

    /**
     * @brief If the getCollectionSchemes function fails, it will return a reference to this empty list.
     */
    const std::vector<ICollectionSchemePtr> EMPTY_COLLECTION_SCHEME_LIST{};

    /**
     * @brief Used internally to hold the collectionSchemes message from the protobuffer
     */
    Schemas::CollectionSchemesMsg::CollectionSchemes mCollectionSchemeListMsg;
};

} // namespace IoTFleetWise
} // namespace Aws
