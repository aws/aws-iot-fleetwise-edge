// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <google/protobuf/message.h>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

DataSenderProtoWriter::DataSenderProtoWriter( CANInterfaceIDTranslator &canIDTranslator,
                                              RawData::BufferManager *rawDataBufferManager )
    : mTriggerTime( 0U )
    , mIDTranslator( canIDTranslator )
    , mRawDataBufferManager( rawDataBufferManager )
{
}

DataSenderProtoWriter::~DataSenderProtoWriter()
{
    google::protobuf::ShutdownProtobufLibrary();
}

void
DataSenderProtoWriter::setupVehicleData( const TriggeredCollectionSchemeData &triggeredCollectionSchemeData,
                                         uint32_t collectionEventID )
{
    mVehicleData.Clear();
    mVehicleData.set_campaign_sync_id( triggeredCollectionSchemeData.metadata.collectionSchemeID );
    mVehicleData.set_decoder_sync_id( triggeredCollectionSchemeData.metadata.decoderID );
    mVehicleData.set_collection_event_id( collectionEventID );
    mTriggerTime = triggeredCollectionSchemeData.triggerTime;
    mVehicleData.set_collection_event_time_ms_epoch( mTriggerTime );
    mMetaDataEstimatedSize = sizeof( collectionEventID ) + sizeof( mTriggerTime ) + STRING_OVERHEAD +
                             triggeredCollectionSchemeData.metadata.collectionSchemeID.size() + STRING_OVERHEAD +
                             triggeredCollectionSchemeData.metadata.decoderID.size();
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
    size_t size{ sizeof( msg.signalID ) + sizeof( relativeTime ) };
    switch ( signalValue.getType() )
    {

    case SignalType::UINT8: {
        // TODO :: Change the datatype of the signal here when the DataPlane supports it
        double signalPhysicalValue = static_cast<double>( signalValue.value.uint8Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::INT8: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.int8Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::UINT16: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.uint16Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::INT16: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.int16Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::UINT32: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.uint32Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::INT32: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.int32Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::UINT64: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.uint64Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::INT64: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.int64Val );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::FLOAT: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.floatVal );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::DOUBLE: {
        double signalPhysicalValue = signalValue.value.doubleVal;
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::BOOLEAN: {
        double signalPhysicalValue = static_cast<double>( signalValue.value.boolVal );
        capturedSignal.set_double_value( signalPhysicalValue );
        size += sizeof( double );
        break;
    }
    case SignalType::STRING: {
        if ( mRawDataBufferManager == nullptr )
        {
            FWE_LOG_WARN( "Raw Data Buffer not initalized so impossible to send data for signal: " +
                          std::to_string( msg.signalID ) );
            return;
        }
        auto loanedRawDataFrame = mRawDataBufferManager->borrowFrame(
            msg.signalID, static_cast<RawData::BufferHandle>( signalValue.value.uint32Val ) );
        if ( loanedRawDataFrame.isNull() )
        {
            FWE_LOG_WARN( "Could not capture the frame from buffer handle" );
            return;
        }
        mRawDataBufferManager->increaseHandleUsageHint(
            msg.signalID, signalValue.value.uint32Val, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER );
        mRawDataBufferManager->decreaseHandleUsageHint(
            msg.signalID,
            signalValue.value.uint32Val,
            RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD );

        auto data = loanedRawDataFrame.getData();
        auto stringSize = loanedRawDataFrame.getSize();
        capturedSignal.set_string_value( reinterpret_cast<const char *>( data ), stringSize );
        mRawDataBufferManager->decreaseHandleUsageHint(
            msg.signalID, signalValue.value.uint32Val, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER );
        size += STRING_OVERHEAD + stringSize;
        break;
    }
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
DataSenderProtoWriter::getNumberOfAppendedMessages() const
{
    return static_cast<size_t>( mVehicleData.captured_signals_size() ) +
           static_cast<size_t>( mVehicleData.dtc_data().active_dtc_codes_size() ) +
           static_cast<size_t>( mVehicleData.s3_objects_size() );
}

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
