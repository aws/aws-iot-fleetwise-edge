// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/ExternalCANDataSource.h"
#include "aws/iotfleetwise/EnumUtility.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

ExternalCANDataSource::ExternalCANDataSource( const CANInterfaceIDTranslator &canIdTranslator,
                                              CANDataConsumer &consumer )
    : mCanIdTranslator{ canIdTranslator }
    , mConsumer{ consumer }
{
}

void
ExternalCANDataSource::ingestMessage( const InterfaceID &interfaceId,
                                      Timestamp timestamp,
                                      uint32_t messageId,
                                      const std::vector<uint8_t> &data )
{
    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    if ( mDecoderDictionary == nullptr )
    {
        return;
    }
    auto channelId = mCanIdTranslator.getChannelNumericID( interfaceId );
    if ( channelId == INVALID_CAN_SOURCE_NUMERIC_ID )
    {
        FWE_LOG_ERROR( "Unknown interface ID: " + interfaceId );
        return;
    }
    if ( timestamp == 0 )
    {
        TraceModule::get().incrementVariable( TraceVariable::POLLING_TIMESTAMP_COUNTER );
        timestamp = mClock->systemTimeSinceEpochMs();
    }
    if ( timestamp < mLastFrameTime )
    {
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::NOT_TIME_MONOTONIC_FRAMES );
    }
    mLastFrameTime = timestamp;
    unsigned traceFrames = channelId + toUType( TraceVariable::READ_SOCKET_FRAMES_0 );
    TraceModule::get().incrementVariable(
        ( traceFrames < static_cast<unsigned>( toUType( TraceVariable::READ_SOCKET_FRAMES_19 ) ) )
            ? static_cast<TraceVariable>( traceFrames )
            : TraceVariable::READ_SOCKET_FRAMES_19 );
    mConsumer.processMessage( channelId, mDecoderDictionary.get(), messageId, data.data(), data.size(), timestamp );
}

void
ExternalCANDataSource::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                                   VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol != VehicleDataSourceProtocol::RAW_SOCKET )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    mDecoderDictionary = std::dynamic_pointer_cast<const CANDecoderDictionary>( dictionary );
    if ( dictionary == nullptr )
    {
        FWE_LOG_TRACE( "Decoder dictionary removed" );
    }
    else
    {
        FWE_LOG_TRACE( "Decoder dictionary updated" );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
