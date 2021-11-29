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

#include "CollectionSchemeIngestion.h"
#include "ICollectionSchemeList.h"
#include "LoggingModule.h"
#include "collection_schemes.pb.h"
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

using namespace Aws::IoTFleetWise::Platform;
using namespace Aws::IoTFleetWise::Schemas;

/**
 * @brief Setting a size limit in bytes for incoming collectionSchemes from cloud
 */
constexpr size_t COLLECTION_SCHEME_LIST_BYTE_SIZE_LIMIT = 128000000;

class CollectionSchemeIngestionList : public ICollectionSchemeList
{
public:
    CollectionSchemeIngestionList();

    ~CollectionSchemeIngestionList();

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
    bool mReady;

    /**
     * @brief Member variable that will hold the vector of collection collectionSchemes
     */
    std::vector<ICollectionSchemePtr> mVectorCollectionSchemePtr;

    /**
     * @brief If the getCollectionSchemes function fails, it will return a reference to this empty list.
     */
    const std::vector<ICollectionSchemePtr> EMPTY_COLLECTION_SCHEME_LIST;

    /**
     * @brief Used internally to hold the collectionSchemes message from the protobuffer
     */
    CollectionSchemesMsg::CollectionSchemes mCollectionSchemeListMsg;

    /**
     * @brief Logging module used to output to logs
     */
    LoggingModule mLogger;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
