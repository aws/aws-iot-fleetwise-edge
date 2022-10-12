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
#include "DataCollectionProtoWriter.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

DataCollectionProtoWriter::DataCollectionProtoWriter( CANInterfaceIDTranslator &canIDTranslator )
    : mTriggerTime( 0U )
    , mIDTranslator( canIDTranslator )
{
}

DataCollectionProtoWriter::~DataCollectionProtoWriter()
{
    google::protobuf::ShutdownProtobufLibrary();
}

void
DataCollectionProtoWriter::setupVehicleData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData,
                                             uint32_t collectionEventID )
{
    mVehicleDataMsgCount = 0U;

    mVehicleData.Clear();
    mVehicleData.set_campaign_arn( triggeredCollectionSchemeData->metaData.collectionSchemeID );
    mVehicleData.set_decoder_arn( triggeredCollectionSchemeData->metaData.decoderID );
    mVehicleData.set_collection_event_id( collectionEventID );
    mTriggerTime = triggeredCollectionSchemeData->triggerTime;
    mVehicleData.set_collection_event_time_ms_epoch( mTriggerTime );
}
void
DataCollectionProtoWriter::append( const CollectedSignal &msg )
{
    auto capturedSignals = mVehicleData.add_captured_signals();
    mVehicleDataMsgCount++;
    capturedSignals->set_relative_time_ms( static_cast<int64_t>( msg.receiveTime ) -
                                           static_cast<int64_t>( mTriggerTime ) );
    capturedSignals->set_signal_id( msg.signalID );
    capturedSignals->set_double_value( msg.value );
}

void
DataCollectionProtoWriter::append( const CollectedCanRawFrame &msg )
{
    auto rawCanFrames = mVehicleData.add_can_frames();
    mVehicleDataMsgCount++;
    rawCanFrames->set_relative_time_ms( static_cast<int64_t>( msg.receiveTime ) -
                                        static_cast<int64_t>( mTriggerTime ) );
    rawCanFrames->set_message_id( msg.frameID );
    rawCanFrames->set_interface_id( mIDTranslator.getInterfaceID( msg.channelId ) );
    rawCanFrames->set_byte_values( reinterpret_cast<char const *>( msg.data.data() ), msg.size );
}

void
DataCollectionProtoWriter::setupDTCInfo( const DTCInfo &msg )
{
    auto dtcData = mVehicleData.mutable_dtc_data();
    dtcData->set_relative_time_ms( static_cast<int64_t>( msg.receiveTime ) - static_cast<int64_t>( mTriggerTime ) );
}

void
DataCollectionProtoWriter::append( const std::string &dtc )
{
    auto dtcData = mVehicleData.mutable_dtc_data();
    mVehicleDataMsgCount++;
    dtcData->add_active_dtc_codes( dtc );
}

void
DataCollectionProtoWriter::append( const GeohashInfo &geohashInfo )
{
    auto geohashProto = mVehicleData.mutable_geohash();
    mVehicleDataMsgCount++;
    geohashProto->set_geohash_string( geohashInfo.mGeohashString );
    geohashProto->set_prev_reported_geohash_string( geohashInfo.mPrevReportedGeohashString );
}

unsigned
DataCollectionProtoWriter::getVehicleDataMsgCount() const
{
    return mVehicleDataMsgCount;
}

bool
DataCollectionProtoWriter::serializeVehicleData( std::string *out ) const
{
    return mVehicleData.SerializeToString( out );
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
