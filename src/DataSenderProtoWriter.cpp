// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderProtoWriter.h"
#include "SignalTypes.h"
#include <array>
#include <google/protobuf/message.h>
#include <memory>
#include <utility>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "LoggingModule.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

DataSenderProtoWriter::DataSenderProtoWriter( CANInterfaceIDTranslator &canIDTranslator,
                                              std::shared_ptr<RawData::BufferManager> rawDataBufferManager )
    : mTriggerTime( 0U )
    , mIDTranslator( canIDTranslator )
    , mRawDataBufferManager( std::move( rawDataBufferManager ) )
{
}

DataSenderProtoWriter::~DataSenderProtoWriter()
{
    google::protobuf::ShutdownProtobufLibrary();
}

void
DataSenderProtoWriter::setupVehicleData(
    std::shared_ptr<const TriggeredCollectionSchemeData> triggeredCollectionSchemeData, uint32_t collectionEventID )
{
    mVehicleData.Clear();
    mVehicleData.set_campaign_sync_id( triggeredCollectionSchemeData->metadata.collectionSchemeID );
    mVehicleData.set_decoder_sync_id( triggeredCollectionSchemeData->metadata.decoderID );
    mVehicleData.set_collection_event_id( collectionEventID );
    mTriggerTime = triggeredCollectionSchemeData->triggerTime;
    mVehicleData.set_collection_event_time_ms_epoch( mTriggerTime );
    mMetaDataEstimatedSize = sizeof( collectionEventID ) + sizeof( mTriggerTime ) + STRING_OVERHEAD +
                             triggeredCollectionSchemeData->metadata.collectionSchemeID.size() + STRING_OVERHEAD +
                             triggeredCollectionSchemeData->metadata.decoderID.size();
    mVehicleDataEstimatedSize = mMetaDataEstimatedSize;
}

void
DataSenderProtoWriter::append( const CollectedSignal &msg )
{
    Schemas::VehicleDataMsg::CapturedSignal capturedSignal;
    auto relativeTime = static_cast<int64_t>( msg.receiveTime ) - static_cast<int64_t>( mTriggerTime );
    capturedSignal.set_relative_time_ms( relativeTime );
    capturedSignal.set_signal_id( msg.signalID );
    auto signalValue = msg.getValue();
    // TODO :: Change the datatype of the signal here when the DataPlane supports it
    double signalPhysicalValue{ 0 };
    size_t size{ sizeof( msg.signalID ) + sizeof( relativeTime ) };
    switch ( signalValue.getType() )
    {

    case SignalType::UINT8:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint8Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::INT8:
        signalPhysicalValue = static_cast<double>( signalValue.value.int8Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::UINT16:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint16Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::INT16:
        signalPhysicalValue = static_cast<double>( signalValue.value.int16Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::UINT32:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint32Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::INT32:
        signalPhysicalValue = static_cast<double>( signalValue.value.int32Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::UINT64:
        signalPhysicalValue = static_cast<double>( signalValue.value.uint64Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::INT64:
        signalPhysicalValue = static_cast<double>( signalValue.value.int64Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::FLOAT:
        signalPhysicalValue = static_cast<double>( signalValue.value.floatVal );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::DOUBLE:
        signalPhysicalValue = signalValue.value.doubleVal;
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::BOOLEAN:
        signalPhysicalValue = static_cast<double>( signalValue.value.boolVal );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    case SignalType::UNKNOWN:
        // UNKNOWN signal should not be processed
        return;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    case SignalType::COMPLEX_SIGNAL:
        FWE_LOG_WARN( "Vision System Data is not supported in the protobuf upload" );
        return;
#endif
    }
    auto capturedSignals = mVehicleData.add_captured_signals();
    *capturedSignals = std::move( capturedSignal );
    mVehicleDataEstimatedSize += size;
}

void
DataSenderProtoWriter::append( const CollectedCanRawFrame &msg )
{
    auto rawCanFrames = mVehicleData.add_can_frames();
    auto relativeTime = static_cast<int64_t>( msg.receiveTime ) - static_cast<int64_t>( mTriggerTime );
    rawCanFrames->set_relative_time_ms( relativeTime );
    rawCanFrames->set_message_id( msg.frameID );
    auto interfaceId = mIDTranslator.getInterfaceID( msg.channelId );
    rawCanFrames->set_interface_id( interfaceId );
    rawCanFrames->set_byte_values( reinterpret_cast<char const *>( msg.data.data() ), msg.size );
    mVehicleDataEstimatedSize += sizeof( relativeTime ) + sizeof( msg.frameID ) + STRING_OVERHEAD + interfaceId.size() +
                                 STRING_OVERHEAD + msg.size;
}

void
DataSenderProtoWriter::setupDTCInfo( const DTCInfo &msg )
{
    auto dtcData = mVehicleData.mutable_dtc_data();
    auto relativeTime = static_cast<int64_t>( msg.receiveTime ) - static_cast<int64_t>( mTriggerTime );
    dtcData->set_relative_time_ms( relativeTime );
    mMetaDataEstimatedSize += sizeof( relativeTime );
    mVehicleDataEstimatedSize += sizeof( relativeTime );
}

void
DataSenderProtoWriter::append( const std::string &dtc )
{
    auto dtcData = mVehicleData.mutable_dtc_data();
    dtcData->add_active_dtc_codes( dtc );
    mVehicleDataEstimatedSize += STRING_OVERHEAD + dtc.size();
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
void
DataSenderProtoWriter::append( const UploadedS3Object &uploadedS3Object )
{
    auto uploadedS3Objects = mVehicleData.add_s3_objects();
    uploadedS3Objects->set_key( uploadedS3Object.key );
    uploadedS3Objects->set_data_format(
        static_cast<Schemas::VehicleDataMsg::DataFormat>( uploadedS3Object.dataFormat ) );
    mVehicleDataEstimatedSize += sizeof( uploadedS3Object.dataFormat ) + STRING_OVERHEAD + uploadedS3Object.key.size();
}
#endif

size_t
DataSenderProtoWriter::getVehicleDataEstimatedSize() const
{
    return mVehicleDataEstimatedSize;
}

bool
DataSenderProtoWriter::isVehicleDataAdded() const
{
    return mVehicleDataEstimatedSize > mMetaDataEstimatedSize;
}

bool
DataSenderProtoWriter::serializeVehicleData( std::string *out ) const
{
    return mVehicleData.SerializeToString( out );
}

void
DataSenderProtoWriter::splitVehicleData( Schemas::VehicleDataMsg::VehicleData &data )
{
    while ( mVehicleData.captured_signals_size() > data.captured_signals_size() )
    {
        data.mutable_captured_signals()->AddAllocated( mVehicleData.mutable_captured_signals()->ReleaseLast() );
    }
    while ( mVehicleData.can_frames_size() > data.can_frames_size() )
    {
        data.mutable_can_frames()->AddAllocated( mVehicleData.mutable_can_frames()->ReleaseLast() );
    }
    while ( mVehicleData.dtc_data().active_dtc_codes_size() > data.dtc_data().active_dtc_codes_size() )
    {
        data.mutable_dtc_data()->mutable_active_dtc_codes()->AddAllocated(
            mVehicleData.mutable_dtc_data()->mutable_active_dtc_codes()->ReleaseLast() );
    }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    while ( mVehicleData.s3_objects_size() > data.s3_objects_size() )
    {
        data.mutable_s3_objects()->AddAllocated( mVehicleData.mutable_s3_objects()->ReleaseLast() );
    }
#endif
}

void
DataSenderProtoWriter::mergeVehicleData( Schemas::VehicleDataMsg::VehicleData &data )
{
    mVehicleData.mutable_captured_signals()->Clear();
    while ( data.captured_signals_size() > 0 )
    {
        mVehicleData.mutable_captured_signals()->AddAllocated( data.mutable_captured_signals()->ReleaseLast() );
    }
    mVehicleData.mutable_can_frames()->Clear();
    while ( data.can_frames_size() > 0 )
    {
        mVehicleData.mutable_can_frames()->AddAllocated( data.mutable_can_frames()->ReleaseLast() );
    }
    mVehicleData.mutable_dtc_data()->mutable_active_dtc_codes()->Clear();
    while ( data.dtc_data().active_dtc_codes_size() > 0 )
    {
        mVehicleData.mutable_dtc_data()->mutable_active_dtc_codes()->AddAllocated(
            data.mutable_dtc_data()->mutable_active_dtc_codes()->ReleaseLast() );
    }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    mVehicleData.mutable_s3_objects()->Clear();
    while ( data.s3_objects_size() > 0 )
    {
        mVehicleData.mutable_s3_objects()->AddAllocated( data.mutable_s3_objects()->ReleaseLast() );
    }
#endif
}

} // namespace IoTFleetWise
} // namespace Aws
