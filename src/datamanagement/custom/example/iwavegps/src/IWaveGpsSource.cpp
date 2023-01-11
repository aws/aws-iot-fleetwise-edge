// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "IWaveGpsSource.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
constexpr const char *IWaveGpsSource::PATH_TO_NMEA;
constexpr const char *IWaveGpsSource::CAN_CHANNEL_NUMBER;
constexpr const char *IWaveGpsSource::CAN_RAW_FRAME_ID;
constexpr const char *IWaveGpsSource::LATITUDE_START_BIT;
constexpr const char *IWaveGpsSource::LONGITUDE_START_BIT;
IWaveGpsSource::IWaveGpsSource( SignalBufferPtr signalBufferPtr )
{
    mSignalBufferPtr = signalBufferPtr;
    mCanChannel = INVALID_CAN_SOURCE_NUMERIC_ID;
    mCanRawFrameId = 0;
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
    char buffer[MAX_BYTES_READ_PER_POLL];

    // Read from NMEA formatted file
    auto bytes = read( mFileHandle, buffer, MAX_BYTES_READ_PER_POLL - 1 );
    buffer[MAX_BYTES_READ_PER_POLL - 1] = 0;
    if ( bytes < 0 )
    {
        mLogger.error( "IWaveGpsSource::pollData", "Error reading from file" );
        return;
    }

    // search for $GPGGA line and extract data from it
    double lastValidLongitude = 0;
    double lastValidLatitude = 0;
    bool foundValid = false;
    int i = 0;
    while ( i < bytes - 7 )
    {
        if ( strncmp( "$GPGGA,", &buffer[i], 7 ) == 0 )
        {
            mGpggaLineCounter++;
            double longitudeRaw = HUGE_VAL;
            double latitudeRaw = HUGE_VAL;
            bool north = true;
            bool east = true;
            int processedBytes = extractLongAndLatitudeFromLine(
                &buffer[i + 7], static_cast<int>( bytes ) - ( i + 7 ), longitudeRaw, latitudeRaw, north, east );
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
        mSignalBufferPtr->push(
            CollectedSignal( getSignalIdFromStartBit( mLatitudeStartBit ), timestamp, lastValidLatitude ) );
        mSignalBufferPtr->push(
            CollectedSignal( getSignalIdFromStartBit( mLongitudeStartBit ), timestamp, lastValidLongitude ) );
    }
    if ( mCyclicLoggingTimer.getElapsedMs().count() > CYCLIC_LOG_PERIOD_MS )
    {
        mLogger.trace( "IWaveGpsSource::pollData",
                       "In the last " + std::to_string( CYCLIC_LOG_PERIOD_MS ) + " millisecond found " +
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
    return latitude >= -90.0 && latitude <= 90.0;
}
bool
IWaveGpsSource::validLongitude( double longitude )
{
    return longitude >= -180.0 && longitude <= 180.0;
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

// compatibility with AbstractVehicleDataSource
bool
IWaveGpsSource::init( const std::vector<VehicleDataSourceConfig> &sourceConfigs )
{
    std::string pathToNmeaSource;
    CANChannelNumericID canChannel = 0;
    CANRawFrameID canRawFrameId = 0;
    uint32_t latitudeStartBit = 0;
    uint32_t longitudeStartBit = 0;

    if ( sourceConfigs.size() > 1 || sourceConfigs.empty() )
    {
        mLogger.error( "IWaveGpsSource::init", "Only one source config is supported" );
        return false;
    }
    auto settingsIterator = sourceConfigs[0].transportProperties.find( std::string( PATH_TO_NMEA ) );
    if ( settingsIterator == sourceConfigs[0].transportProperties.end() )
    {
        mLogger.error( "IWaveGpsSource::init", "Could not find nmeaFilePath in the config" );
        return false;
    }
    else
    {
        pathToNmeaSource = settingsIterator->second;
    }

    if ( extractIntegerFromConfig( sourceConfigs, CAN_CHANNEL_NUMBER, canChannel ) &&
         extractIntegerFromConfig( sourceConfigs, CAN_RAW_FRAME_ID, canRawFrameId ) &&
         extractIntegerFromConfig( sourceConfigs, LONGITUDE_START_BIT, longitudeStartBit ) &&
         extractIntegerFromConfig( sourceConfigs, LATITUDE_START_BIT, latitudeStartBit ) )
    {
        return init( pathToNmeaSource,
                     canChannel,
                     canRawFrameId,
                     static_cast<uint16_t>( latitudeStartBit ),
                     static_cast<uint16_t>( longitudeStartBit ) );
    }
    return false;
}

bool
IWaveGpsSource::extractIntegerFromConfig( const std::vector<VehicleDataSourceConfig> &sourceConfigs,
                                          const std::string key,
                                          uint32_t &extractedValue )
{
    auto settingsIterator = sourceConfigs[0].transportProperties.find( std::string( key ) );
    if ( settingsIterator == sourceConfigs[0].transportProperties.end() )
    {
        mLogger.error( "IWaveGpsSource::init", "Could not find " + key + " in the config" );
        return false;
    }
    else
    {
        try
        {
            extractedValue = static_cast<uint32_t>( std::stoul( settingsIterator->second ) );
        }
        catch ( const std::exception &e )
        {
            mLogger.error( "IWaveGpsSource::init",
                           "Could not cast the " + key + ", invalid input: " + std::string( e.what() ) );
            return false;
        }
    }
    return true;
}
bool
IWaveGpsSource::connect()
{
    mFileHandle = open( mPathToNmeaSource.c_str(), O_RDONLY | O_NOCTTY );
    if ( mFileHandle == -1 )
    {
        mLogger.error( "IWaveGpsSource::init", "Could not open GPS NMEA file:" + mPathToNmeaSource );
        return false;
    }
    return true;
}

bool
IWaveGpsSource::disconnect()
{
    return ( close( mFileHandle ) == 0 );
}
bool
IWaveGpsSource::isAlive()
{
    return isRunning();
}
void
IWaveGpsSource::suspendDataAcquisition()
{
    setFilter( INVALID_CAN_SOURCE_NUMERIC_ID, 0 );
}
void
IWaveGpsSource::resumeDataAcquisition()
{
    setFilter( mCanChannel, mCanRawFrameId );
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
#endif