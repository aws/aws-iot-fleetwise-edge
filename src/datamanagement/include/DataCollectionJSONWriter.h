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
#include "CollectionInspectionAPITypes.h"
#include "GeohashFunctionNode.h"
#include "OBDDataTypes.h"
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
    DataCollectionJSONWriter();
    ~DataCollectionJSONWriter();

    void setupEvent( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData, uint32_t collectionEventID );

    void append( const CollectedSignal &msg );
    void append( const CollectedCanRawFrame &msg );
    void append( const GeohashInfo &geohashInfo );
    void flushToFile( const uint64_t &timestamp );
    unsigned getJSONMessageCount( void );

private:
    Json::Value mEvent;
    Json::Value mMessages;
    Json::UInt64 mTriggerTime;
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws