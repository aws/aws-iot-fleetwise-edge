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

// Includes
#include "DataCollectionJSONWriter.h"
#include <fstream>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

DataCollectionJSONWriter::DataCollectionJSONWriter()
{
    mMessages = Json::arrayValue;
    mTriggerTime = 0;
}

DataCollectionJSONWriter::~DataCollectionJSONWriter()
{
}

void
DataCollectionJSONWriter::append( const CollectedCanRawFrame &msg )
{
    Json::Value message;
    message["CANFrame"]["messageID"] = (Json::UInt)msg.frameID;
    message["CANFrame"]["nodeID"] = (Json::UInt)msg.channelId;
    message["CANFrame"]["relativeTimeMS"] = ( Json::Int64 )( ( msg.receiveTime ) - mTriggerTime );

    Json::Value signals( Json::arrayValue );
    Json::Value signal;
    for ( size_t i = 0; i < msg.size; ++i )
    {
        signal["CANFrame"]["byteValue"] = msg.data[i];
        signals.append( signal );
    }
    message["CANFrame"]["byteValues"] = signals;
    mMessages.append( message );
}

void
DataCollectionJSONWriter::append( const CollectedSignal &msg )
{
    Json::Value message;
    message["CapturedSignal"]["signalID"] = (Json::UInt)msg.signalID;
    message["CapturedSignal"]["relativeTimeMS"] = ( Json::Int64 )( ( msg.receiveTime ) - mTriggerTime );
    message["CapturedSignal"]["doubleValue"] = msg.value;
    mMessages.append( message );
}

void
DataCollectionJSONWriter::append( const GeohashInfo &geohashInfo )
{
    Json::Value message;
    message["Geohash"] = geohashInfo.mGeohashString;
    message["PrevReportedGeohash"] = geohashInfo.mPrevReportedGeohashString;
    mMessages.append( message );
}

void
DataCollectionJSONWriter::flushToFile( const uint64_t &timestamp )
{

    std::ofstream file_id;
    file_id.open( "IoTFleetWise-Data-" + std::to_string( timestamp ) + ".json" );

    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "   "; // or whatever you like
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    mEvent["Messages"] = mMessages;
    writer->write( mEvent, &file_id );

    file_id.close();
}

void
DataCollectionJSONWriter::setupEvent( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData,
                                      uint32_t collectionEventID )
{
    mEvent.clear();
    mMessages.clear();
    mEvent["Event"]["collectionSchemeARN"] = triggeredCollectionSchemeData->metaData.collectionSchemeID;
    mEvent["Event"]["decoderARN"] = triggeredCollectionSchemeData->metaData.decoderID;
    mEvent["Event"]["collectionEventID"] = (Json::UInt)collectionEventID;
    mEvent["Event"]["collectionEventTimeMSEpoch"] = ( Json::UInt64 )( triggeredCollectionSchemeData->triggerTime );
    mTriggerTime = triggeredCollectionSchemeData->triggerTime;
}

unsigned
DataCollectionJSONWriter::getJSONMessageCount( void )
{
    return mMessages.size();
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws