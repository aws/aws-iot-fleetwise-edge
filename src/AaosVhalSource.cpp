// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AaosVhalSource.h"
#include "CollectionInspectionAPITypes.h"
#include "QueueTypes.h"
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

// NOLINT below due to C++17 warning of redundant declarations that are required to maintain C++14 compatibility
constexpr const char *AaosVhalSource::CAN_CHANNEL_NUMBER; // NOLINT
constexpr const char *AaosVhalSource::CAN_RAW_FRAME_ID;   // NOLINT
AaosVhalSource::AaosVhalSource( SignalBufferDistributorPtr signalBufferDistributor )
    : mSignalBufferDistributor{ std::move( signalBufferDistributor ) }
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
AaosVhalSource::setVehicleProperty( SignalID signalId, const DecodedSignalValue &value )
{
    auto signalType = SignalType::DOUBLE;
    auto timestamp = mClock->systemTimeSinceEpochMs();
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal::fromDecodedSignal( signalId, timestamp, value, signalType ) );

    mSignalBufferDistributor->push( CollectedDataFrame( collectedSignalsGroup ) );
}

void
AaosVhalSource::pollData()
{
}

} // namespace IoTFleetWise
} // namespace Aws
