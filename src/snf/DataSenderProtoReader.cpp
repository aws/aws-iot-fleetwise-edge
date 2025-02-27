// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/DataSenderProtoReader.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <cstdint>
#include <google/protobuf/message.h>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

DataSenderProtoReader::DataSenderProtoReader( CANInterfaceIDTranslator &canIDTranslator )
    : mIDTranslator( canIDTranslator )
{
}

DataSenderProtoReader::~DataSenderProtoReader()
{
    google::protobuf::ShutdownProtobufLibrary();
}

bool
DataSenderProtoReader::setupVehicleData( const std::string &data )
{
    mVehicleData.Clear();
    return mVehicleData.ParseFromString( data );
}

bool
DataSenderProtoReader::deserializeVehicleData( TriggeredCollectionSchemeData &out )
{
    out.eventID = mVehicleData.collection_event_id();
    out.triggerTime = mVehicleData.collection_event_time_ms_epoch();

    // metadata
    out.metadata.collectionSchemeID = mVehicleData.campaign_sync_id();
    out.metadata.decoderID = mVehicleData.decoder_sync_id();

    // signals
    for ( auto i = 0; i < mVehicleData.captured_signals_size(); ++i )
    {
        auto protoSignal = mVehicleData.captured_signals( i );
        CollectedSignal signal{};
        signal.signalID = protoSignal.signal_id();
        auto receiveTime =
            protoSignal.relative_time_ms() + static_cast<int64_t>( mVehicleData.collection_event_time_ms_epoch() );
        if ( receiveTime >= 0 )
        {
            signal.receiveTime = static_cast<Timestamp>( receiveTime );
        }
        signal.value.setVal( protoSignal.double_value(), SignalType::DOUBLE );
        out.signals.emplace_back( signal );
    }

    // dtc info
    if ( mVehicleData.has_dtc_data() )
    {
        DTCInfo info;
        // NOTE: mSID is purposefully omitted because it is not serialized
        auto receiveTime = mVehicleData.dtc_data().relative_time_ms() +
                           static_cast<int64_t>( mVehicleData.collection_event_time_ms_epoch() );
        if ( receiveTime >= 0 )
        {
            info.receiveTime = static_cast<Timestamp>( receiveTime );
        }
        for ( auto code : mVehicleData.dtc_data().active_dtc_codes() )
        {
            info.mDTCCodes.emplace_back( code );
        }
        out.mDTCInfo = info;
    }

    return true;
}

} // namespace IoTFleetWise
} // namespace Aws
