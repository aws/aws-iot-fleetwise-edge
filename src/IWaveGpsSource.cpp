// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IWaveGpsSource.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <boost/lockfree/queue.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

// NOLINT below due to C++17 warning of redundant declarations that are required to maintain C++14 compatibility
constexpr const char *IWaveGpsSource::PATH_TO_NMEA;        // NOLINT
constexpr const char *IWaveGpsSource::CAN_CHANNEL_NUMBER;  // NOLINT
constexpr const char *IWaveGpsSource::CAN_RAW_FRAME_ID;    // NOLINT
constexpr const char *IWaveGpsSource::LATITUDE_START_BIT;  // NOLINT
constexpr const char *IWaveGpsSource::LONGITUDE_START_BIT; // NOLINT
IWaveGpsSource::IWaveGpsSource( SignalBufferPtr signalBufferPtr )
    : mSignalBufferPtr{ std::move( signalBufferPtr ) }
{
}

bool
IWaveGpsSource::init( const std::string &pathToNmeaSource,
                      CANChannelNumericID canChannel,
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
    mPathToNmeaSource = pathToNmeaSource;
    mCanChannel = canChannel;
    mCanRawFrameId = canRawFrameId;
    setFilter( mCanChannel, mCanRawFrameId );
    mCyclicLoggingTimer.reset();
    return true;
}
const char *
IWaveGpsSource::getThreadName()
{
    return "IWaveGpsSource";
}

void
IWaveGpsSource::pollData()
{
    // Read from NMEA formatted file
    auto bytes = read( mFileHandle, mBuffer, MAX_BYTES_READ_PER_POLL - 1 );
    if ( bytes < 0 )
    {
        FWE_LOG_ERROR( "Error reading from file" );
        return;
    }

    // search for $GPGGA line and extract data from it
    double lastValidLongitude = 0;
    double lastValidLatitude = 0;
    bool foundValid = false;
    int i = 0;
    while ( i < bytes - 7 )
    {
        if ( strncmp( "$GPGGA,", &mBuffer[i], 7 ) == 0 )
        {
            mGpggaLineCounter++;
            double longitudeRaw = HUGE_VAL;
            double latitudeRaw = HUGE_VAL;
            bool north = true;
            bool east = true;
            int processedBytes = extractLongAndLatitudeFromLine(
                &mBuffer[i + 7], static_cast<int>( bytes ) - ( i + 7 ), longitudeRaw, latitudeRaw, north, east );
            i += processedBytes;
            double longitude = convertDmmToDdCoordinates( longitudeRaw, east );
            double latitude = convertDmmToDdCoordinates( latitudeRaw, north );
            if ( validLatitude( latitude ) && validLongitude( longitude ) )
            {
                lastValidLongitude = longitude;
                lastValidLatitude = latitude;
                foundValid = true;
            }
        }
        i++;
    }

    // If values were found pass them on as Signals similar to CAN Signals
    if ( foundValid && mSignalBufferPtr != nullptr )
    {
        mValidCoordinateCounter++;
        auto timestamp = mClock->systemTimeSinceEpochMs();

        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
        if ( !mSignalBufferPtr->push(
                 CollectedSignal( getSignalIdFromStartBit( mLatitudeStartBit ), timestamp, lastValidLatitude ) ) )
        {
            TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
            FWE_LOG_WARN( "Signal buffer full" );
        }

        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
        if ( !mSignalBufferPtr->push(
                 CollectedSignal( getSignalIdFromStartBit( mLongitudeStartBit ), timestamp, lastValidLongitude ) ) )
        {
            TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
            FWE_LOG_WARN( "Signal buffer full" );
        }
    }
    if ( mCyclicLoggingTimer.getElapsedMs().count() > CYCLIC_LOG_PERIOD_MS )
    {
        FWE_LOG_TRACE( "In the last " + std::to_string( CYCLIC_LOG_PERIOD_MS ) + " millisecond found " +
                       std::to_string( mGpggaLineCounter ) + " lines with $GPGGA and extracted " +
                       std::to_string( mValidCoordinateCounter ) + " valid coordinates from it" );
        mCyclicLoggingTimer.reset();
        mGpggaLineCounter = 0;
        mValidCoordinateCounter = 0;
    }
}

bool
IWaveGpsSource::validLatitude( double latitude )
{
    return ( latitude >= -90.0 ) && ( latitude <= 90.0 );
}
bool
IWaveGpsSource::validLongitude( double longitude )
{
    return ( longitude >= -180.0 ) && ( longitude <= 180.0 );
}

double
IWaveGpsSource::convertDmmToDdCoordinates( double dmm, bool positive )
{
    double degrees = floor( dmm / 100.0 );
    degrees += ( dmm / 100.0 - degrees ) * ( 100.0 / 60.0 );
    if ( !positive )
    {
        degrees *= -1.0;
    }
    return degrees;
}

int
IWaveGpsSource::extractLongAndLatitudeFromLine(
    const char *start, int limit, double &longitude, double &latitude, bool &north, bool &east )
{
    int commaCounter = 0;
    int lastCommaPosition = 0;
    int i = 0;
    for ( i = 0; i < limit; i++ )
    {
        // the line is comma separated
        if ( start[i] == ',' )
        {
            commaCounter++;
            // First comes latitude
            if ( commaCounter == 1 )
            {
                if ( i - lastCommaPosition > 1 )
                {
                    char *end = nullptr;
                    latitude = std::strtod( &start[i + 1], &end );
                    if ( end == &start[i + 1] )
                    {
                        latitude = HUGE_VAL;
                    }
                }
            }
            // Then 'N' or 'S' for north south
            else if ( commaCounter == 2 )
            {
                north = start[i + 1] == 'N';
            }

            // Then the longitude
            else if ( commaCounter == 3 )
            {
                if ( i - lastCommaPosition > 1 )
                {
                    char *end = nullptr;
                    longitude = std::strtod( &start[i + 1], &end );
                    if ( end == &start[i + 1] )
                    {
                        longitude = HUGE_VAL;
                    }
                }
            }
            // Then 'E' or 'W' for East or West
            else if ( commaCounter == 4 )
            {
                east = start[i + 1] == 'E';
                return i;
            }
            lastCommaPosition = i;
        }
    }
    return i;
}

bool
IWaveGpsSource::connect()
{
    mFileHandle = open( mPathToNmeaSource.c_str(), O_RDONLY | O_NOCTTY );
    if ( mFileHandle == -1 )
    {
        FWE_LOG_ERROR( "Could not open GPS NMEA file:" + mPathToNmeaSource );
        return false;
    }
    return true;
}

bool
IWaveGpsSource::disconnect()
{
    if ( close( mFileHandle ) != 0 )
    {
        return false;
    }
    mFileHandle = -1;
    return true;
}

} // namespace IoTFleetWise
} // namespace Aws
