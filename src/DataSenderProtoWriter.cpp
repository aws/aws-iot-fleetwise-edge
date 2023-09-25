// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderProtoWriter.h"
#include "SignalTypes.h"
#include <array>
#include <google/protobuf/message.h>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

DataSenderProtoWriter::DataSenderProtoWriter( CANInterfaceIDTranslator &canIDTranslator )
    : mTriggerTime( 0U )
    , mIDTranslator( canIDTranslator )
{
}

DataSenderProtoWriter::~DataSenderProtoWriter()
{
    google::protobuf::ShutdownProtobufLibrary();
}

void
DataSenderProtoWriter::setupVehicleData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData,
                                         uint32_t collectionEventID )
{
    mVehicleDataMsgCount = 0U;

    mVehicleData.Clear();
    mVehicleData.set_campaign_sync_id( triggeredCollectionSchemeData->metadata.collectionSchemeID );
    mVehicleData.set_decoder_sync_id( triggeredCollectionSchemeData->metadata.decoderID );
    mVehicleData.set_collection_event_id( collectionEventID );
    mTriggerTime = triggeredCollectionSchemeData->triggerTime;
    mVehicleData.set_collection_event_time_ms_epoch( mTriggerTime );
}
void
DataSenderProtoWriter::append( const CollectedSignal &msg )
{
    auto capturedSignals = mVehicleData.add_captured_signals();
    mVehicleDataMsgCount++;
    capturedSignals->set_relative_time_ms( static_cast<int64_t>( msg.receiveTime ) -
                                           static_cast<int64_t>( mTriggerTime ) );
    capturedSignals->set_signal_id( msg.signalID );
    auto signalValue = msg.getValue();
    // TODO :: Change the datatype of the signal here when the DataPlane supports it
    double signalPhysicalValue{ 0 };
    switch ( signalValue.getType() )
    {

    case SignalType::UINT8:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint8Val );
        break;
    case SignalType::INT8:
        signalPhysicalValue = static_cast<double>( signalValue.value.int8Val );
        break;
    case SignalType::UINT16:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint16Val );
        break;
    case SignalType::INT16:
        signalPhysicalValue = static_cast<double>( signalValue.value.int16Val );
        break;
    case SignalType::UINT32:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint32Val );
        break;
    case SignalType::INT32:
        signalPhysicalValue = static_cast<double>( signalValue.value.int32Val );
        break;
    case SignalType::UINT64:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint64Val );
        break;
    case SignalType::INT64:
        signalPhysicalValue = static_cast<double>( signalValue.value.int64Val );
        break;
    case SignalType::FLOAT:
        signalPhysicalValue = static_cast<double>( signalValue.value.floatVal );
        break;
    case SignalType::DOUBLE:
        signalPhysicalValue = signalValue.value.doubleVal;
        break;
    case SignalType::BOOLEAN:
        signalPhysicalValue = static_cast<double>( signalValue.value.boolVal );
        break;
    default:
        signalPhysicalValue = signalValue.value.doubleVal;
        break;
    }
    capturedSignals->set_double_value( signalPhysicalValue );
}

void
DataSenderProtoWriter::append( const CollectedCanRawFrame &msg )
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
DataSenderProtoWriter::setupDTCInfo( const DTCInfo &msg )
{
    auto dtcData = mVehicleData.mutable_dtc_data();
    dtcData->set_relative_time_ms( static_cast<int64_t>( msg.receiveTime ) - static_cast<int64_t>( mTriggerTime ) );
}

void
DataSenderProtoWriter::append( const std::string &dtc )
{
    auto dtcData = mVehicleData.mutable_dtc_data();
    mVehicleDataMsgCount++;
    dtcData->add_active_dtc_codes( dtc );
}

void
DataSenderProtoWriter::append( const GeohashInfo &geohashInfo )
{
    auto geohashProto = mVehicleData.mutable_geohash();
    mVehicleDataMsgCount++;
    geohashProto->set_geohash_string( geohashInfo.mGeohashString );
    geohashProto->set_prev_reported_geohash_string( geohashInfo.mPrevReportedGeohashString );
}

unsigned
DataSenderProtoWriter::getVehicleDataMsgCount() const
{
    return mVehicleDataMsgCount;
}

bool
DataSenderProtoWriter::serializeVehicleData( std::string *out ) const
{
    return mVehicleData.SerializeToString( out );
}

} // namespace IoTFleetWise
} // namespace Aws
