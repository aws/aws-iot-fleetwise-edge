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
#include "CANDataTypes.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "GeohashInfo.h"
#include "LoggingModule.h"
#include "OBDDataTypes.h"
#include <boost/filesystem.hpp>
#include <json/json.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::DataInspection;

/**
 * @brief A Json based writer of the collected data.
 */
class DataCollectionJSONWriter
{
public:
    /**
     * @brief Construct a new Data Collection JSON Writer object
     *
     * @param persistencyPath     Path to file system where files will be written for durable storage.
     */
    DataCollectionJSONWriter( std::string persistencyPath );
    void setupEvent( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData, uint32_t collectionEventID );

    void append( const CollectedSignal &msg );
    void append( const CollectedCanRawFrame &msg );
    void append( const GeohashInfo &geohashInfo );

    /**
     * @brief Write the contents of the event to a file.
     *
     * @return std::pair with
     * the first element containing the file path and name of the created file. The file extension will be
     * json for uncompressed files and snappy for compressed files.
     * the second element indicating whether the file was compressed or not.
     */
    std::pair<boost::filesystem::path, bool> flushToFile();

    unsigned getJSONMessageCount() const;

private:
    /**
     * @brief Write the event data to a file
     *
     * @param fileBaseName Base file name to use.
     * Do not provide a file extension.
     * The extension will be added based on whether the file contents are compressed or not.
     * For uncompressed file, the extension will be ".json". For compressed files, it will be ".snappy"
     *
     * @return std::pair with
     * the first element containing the file name of the created file. The file extension will be
     * json for uncompressed files and snappy for compressed files.
     * the second element indicating whether the file was compressed or not.
     */
    std::pair<boost::filesystem::path, bool> flushToFile( const std::string &fileBaseName );

    Json::Value mEvent;
    Json::Value mMessages{ Json::arrayValue };
    Json::UInt64 mTriggerTime{ 0 };
    std::string mPersistencyPath;

    /**
     * @brief Whether the contents of the JSON file will be compressed or not.
     * This is based on the metadata available in the CollectionSchemeData and is modified as part of
     * setupEvent method.
     *
     */
    bool mShouldCompress{ false };

    /**
     * @brief Logger used to log output.
     */
    LoggingModule mLogger;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws