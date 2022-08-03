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

// Refer to the following for definitions of significand and exponent:
// https://en.wikipedia.org/wiki/Double-precision_floating-point_format
#define DBL_SIGNIFICAND_BITS 52U
#define DBL_SIGNIFICAND_MASK ( ( 1ULL << DBL_SIGNIFICAND_BITS ) - 1U )
#define DBL_SIGNIFICAND_FULL_BITS ( DBL_SIGNIFICAND_BITS + 1U ) // Including 'hidden' 53rd bit
#define DBL_EXPONENT_BITS 11U
#define DBL_EXPONENT_MASK ( ( 1U << DBL_EXPONENT_BITS ) - 1U )

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
DataCollectionProtoWriter::convertToPeculiarFloat( double physicalValue, uint32_t &quotient, uint32_t &divisor )
{
    /*
    IEEE 754 double format:
        SEEEEEEEEEEEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
        ^^         ^^                                                  ^
        ||         ||                                                  |
        ||         |----------------------------------------------------- 52-bit fraction
        |------------ 11-bit exponent (biased)
        -- 1-bit sign
    Formula:
        number = (-1)^sign * (((2^52 + fraction) / 2^52) * (2^(exponent - 1023))

    Output formula:
        number = quotient / divisor

    The following code translates the IEEE 754 double format to the quotient and divisor format by
    mapping the fraction to the quotient and the exponent to the divisor, clipping and translating
    as appropriate.
    */

    // Decode IEEE 754 'binary64' (a.k.a. 'double') format (ignore the sign, it is encoded separately):
    uint64_t raw = 0x0ULL;
    memcpy( &raw, &physicalValue, sizeof( raw ) );
    uint64_t fraction = raw & DBL_SIGNIFICAND_MASK;
    int exponent = static_cast<int>( ( raw >> DBL_SIGNIFICAND_BITS ) & DBL_EXPONENT_MASK );

    fraction |= 1ULL << ( DBL_SIGNIFICAND_FULL_BITS - 1U ); // Set the 'hidden' 53rd bit
    exponent -= 1023;                                       // Remove exponent bias

    if ( exponent >= UINT32_WIDTH ) // physicalValue is too big, infinite or NaN: return UINT32_MAX
    {
        quotient = UINT32_MAX;
        divisor = 1U;
        return;
    }
    if ( exponent <= -UINT32_WIDTH ) // physicalValue is too small or zero: return zero
    {
        quotient = 0U;
        divisor = 1U;
        return;
    }
    // Note: exponent is now in the range -31 to 31

    if ( exponent < 0 ) // physicalValue is less than 1.0
    {
        // Multiply by 2^exponent, i.e. right shift by -exponent, then set the exponent to zero,
        // so that the divisor will be 2^31 below:
        fraction >>= static_cast<unsigned>( -exponent );
        exponent = 0;
    }
    // Throw away lowest 21-bits of precision:
    quotient = static_cast<uint32_t>( fraction >> ( DBL_SIGNIFICAND_FULL_BITS - UINT32_WIDTH ) );
    divisor = 1U << ( ( UINT32_WIDTH - 1U ) - static_cast<unsigned>( exponent ) );
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
