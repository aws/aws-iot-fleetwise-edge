// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "ExternalGpsSource.h"
#include "LoggingModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
constexpr const char *ExternalGpsSource::CAN_CHANNEL_NUMBER;
constexpr const char *ExternalGpsSource::CAN_RAW_FRAME_ID;
constexpr const char *ExternalGpsSource::LATITUDE_START_BIT;
constexpr const char *ExternalGpsSource::LONGITUDE_START_BIT;
ExternalGpsSource::ExternalGpsSource( SignalBufferPtr signalBufferPtr )
    : mSignalBufferPtr{ std::move( signalBufferPtr ) }
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
    return "ExternalGpsSource";
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
    mSignalBufferPtr->push( CollectedSignal( latitudeSignalId, timestamp, latitude ) );
    mSignalBufferPtr->push( CollectedSignal( longitudeSignalId, timestamp, longitude ) );
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

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
#endif
