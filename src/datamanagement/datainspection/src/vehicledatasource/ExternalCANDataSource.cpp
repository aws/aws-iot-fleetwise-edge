// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "ExternalCANDataSource.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include "TraceModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform::Utility;

ExternalCANDataSource::ExternalCANDataSource( CANDataConsumer &consumer )
    : mConsumer{ consumer }
{
}

void
ExternalCANDataSource::ingestMessage( CANChannelNumericID channelId,
                                      Timestamp timestamp,
                                      uint32_t messageId,
                                      const std::vector<uint8_t> &data )
{
    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    if ( mDecoderDictionary == nullptr )
    {
        return;
    }
    if ( timestamp == 0 )
    {
        TraceModule::get().incrementVariable( TraceVariable::CAN_POLLING_TIMESTAMP_COUNTER );
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
    mConsumer.processMessage( channelId, mDecoderDictionary, messageId, data.data(), data.size(), timestamp );
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

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
