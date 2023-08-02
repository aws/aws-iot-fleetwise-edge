// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "AaosVhalSource.h"
#include "LoggingModule.h"
#include "TraceModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
constexpr const char *AaosVhalSource::CAN_CHANNEL_NUMBER;
constexpr const char *AaosVhalSource::CAN_RAW_FRAME_ID;
AaosVhalSource::AaosVhalSource( SignalBufferPtr signalBufferPtr )
    : mSignalBufferPtr{ std::move( signalBufferPtr ) }
{
}

bool
AaosVhalSource::init( CANChannelNumericID canChannel, CANRawFrameID canRawFrameId )
{
    if ( canChannel == INVALID_CAN_SOURCE_NUMERIC_ID )
    {
        return false;
    }
    mCanChannel = canChannel;
    mCanRawFrameId = canRawFrameId;
    setFilter( mCanChannel, mCanRawFrameId );
    return true;
}
const char *
AaosVhalSource::getThreadName()
{
    return "AaosVhalSource";
}

std::vector<std::array<uint32_t, 4>>
AaosVhalSource::getVehiclePropertyInfo()
{
    std::vector<std::array<uint32_t, 4>> propertyInfo;
    auto signalInfo = getSignalInfo();
    for ( const auto &signal : signalInfo )
    {
        propertyInfo.push_back( std::array<uint32_t, 4>{
            static_cast<uint32_t>( signal.mOffset ), // Vehicle property ID
            signal.mFirstBitPosition,                // Area index
            signal.mSizeInBits,                      // Result index
            signal.mSignalID                         // Signal ID
        } );
    }
    return propertyInfo;
}

void
AaosVhalSource::setVehicleProperty( SignalID signalId, double value )
{
    auto timestamp = mClock->systemTimeSinceEpochMs();
    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
    if ( !mSignalBufferPtr->push( CollectedSignal( signalId, timestamp, value ) ) )
    {
        TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
        FWE_LOG_WARN( "Signal buffer full" );
    }
}

void
AaosVhalSource::pollData()
{
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
