// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
using namespace Aws::IoTFleetWise::Platform::Linux;
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
    CollectionSchemePtr getCollectionScheme();

private:
    std::mutex mMutex;
    CollectionSchemePtr mCollectionScheme;
    LoggingModule mLogger;
    std::string mPath;
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
