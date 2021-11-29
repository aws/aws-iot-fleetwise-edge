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

// Includes
#include "CollectionScheme.h"
#include "LoggingModule.h"
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform;
/**
 * @brief A Parser for a Json representation of the  Collection Scheme.
 */
class CollectionSchemeJSONParser
{
public:
    /**
     * @brief Constructor.
     * @param path to the Json file.
     */
    CollectionSchemeJSONParser( const std::string &path );
    virtual ~CollectionSchemeJSONParser();
    /**
     * @brief Parses the Json file following the scheme defined.
     * @return true if success.
     */
    bool parse();
    /**
     * @brief reloads the Json file if it has changed. Locks any new access to
     * underlying deserialized object till the parsing ends.
     * @return true if success.
     */
    bool reLoad();
    /**
     * @brief getter for the Collection Scheme Object.
     * @return Shared pointer to the object. Waits to return on a Mutex if
     * the object is being constructed.
     */
    const CollectionSchemePtr getCollectionScheme();

private:
    std::mutex mMutex;
    CollectionSchemePtr mCollectionScheme;
    LoggingModule mLogger;
    std::string mPath;
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws