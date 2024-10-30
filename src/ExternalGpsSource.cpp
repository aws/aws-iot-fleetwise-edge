// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExternalGpsSource.h"
#include "CollectionInspectionAPITypes.h"
#include "LoggingModule.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

// NOLINT below due to C++17 warning of redundant declarations that are required to maintain C++14 compatibility
constexpr const char *ExternalGpsSource::CAN_CHANNEL_NUMBER;  // NOLINT
constexpr const char *ExternalGpsSource::CAN_RAW_FRAME_ID;    // NOLINT
constexpr const char *ExternalGpsSource::LATITUDE_START_BIT;  // NOLINT
constexpr const char *ExternalGpsSource::LONGITUDE_START_BIT; // NOLINT
ExternalGpsSource::ExternalGpsSource( SignalBufferDistributorPtr signalBufferDistributor )
    : mSignalBufferDistributor{ std::move( signalBufferDistributor ) }
{
}

bool
ExternalGpsSource::init( CANChannelNumericID canChannel,
                         CANRawFrameID canRawFrameId,
                         uint16_t latitudeStartBit,
                         uint16_t longitudeStartBit )
{
    if ( canChannel == INVALID_CAN_SOURCE_NUMERIC_ID )
    {
        return false;
    }
    mLatitudeStartBit = latitudeStartBit;
    mLongitudeStartBit = longitudeStartBit;
    mCanChannel = canChannel;
    mCanRawFrameId = canRawFrameId;
    setFilter( mCanChannel, mCanRawFrameId );
    return true;
}
const char *
ExternalGpsSource::getThreadName()
{
    return "ExternalGpsSrc";
}

void
ExternalGpsSource::setLocation( double latitude, double longitude )
{
    if ( ( !validLatitude( latitude ) ) || ( !validLongitude( longitude ) ) )
    {
        FWE_LOG_WARN( "Invalid location: Latitude: " + std::to_string( latitude ) +
                      ", Longitude: " + std::to_string( longitude ) );
        return;
    }
    FWE_LOG_TRACE( "Latitude: " + std::to_string( latitude ) + ", Longitude: " + std::to_string( longitude ) );
    auto latitudeSignalId = getSignalIdFromStartBit( mLatitudeStartBit );
    auto longitudeSignalId = getSignalIdFromStartBit( mLongitudeStartBit );
    if ( ( latitudeSignalId == INVALID_SIGNAL_ID ) || ( longitudeSignalId == INVALID_SIGNAL_ID ) )
    {
        FWE_LOG_WARN( "Latitude or longitude not in decoder manifest" );
        return;
    }
    auto timestamp = mClock->systemTimeSinceEpochMs();
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( latitudeSignalId, timestamp, latitude, SignalType::DOUBLE ) );
    collectedSignalsGroup.push_back( CollectedSignal( longitudeSignalId, timestamp, longitude, SignalType::DOUBLE ) );

    mSignalBufferDistributor->push( CollectedDataFrame( std::move( collectedSignalsGroup ) ) );
}

void
ExternalGpsSource::pollData()
{
}

bool
ExternalGpsSource::validLatitude( double latitude )
{
    return ( latitude >= -90.0 ) && ( latitude <= 90.0 );
}
bool
ExternalGpsSource::validLongitude( double longitude )
{
    return ( longitude >= -180.0 ) && ( longitude <= 180.0 );
}

} // namespace IoTFleetWise
} // namespace Aws
