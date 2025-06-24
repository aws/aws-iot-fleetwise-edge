// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/IWaveGpsSource.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

IWaveGpsSource::IWaveGpsSource( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                std::string pathToNmeaSource,
                                std::string latitudeSignalName,
                                std::string longitudeSignalName,
                                uint32_t pollIntervalMs )
    : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mPathToNmeaSource( std::move( pathToNmeaSource ) )
    , mLatitudeSignalName( std::move( latitudeSignalName ) )
    , mLongitudeSignalName( std::move( longitudeSignalName ) )
    , mPollIntervalMs( pollIntervalMs )
{
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
    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
    double lastValidLongitude = 0;
    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
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
            // coverity[INTEGER_OVERFLOW:FALSE] bytes - ( i + 7 ) won't overflow as i depends on bytes, which is limited
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

    if ( foundValid )
    {
        mValidCoordinateCounter++;

        std::vector<std::pair<std::string, DecodedSignalValue>> values;
        values.emplace_back( mLatitudeSignalName, DecodedSignalValue( lastValidLatitude, SignalType::DOUBLE ) );
        values.emplace_back( mLongitudeSignalName, DecodedSignalValue( lastValidLongitude, SignalType::DOUBLE ) );
        mNamedSignalDataSource->ingestMultipleSignalValues( 0, values );
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
    if ( mPollIntervalMs == 0 )
    {
        FWE_LOG_ERROR( "Zero poll interval time" );
        return false;
    }

    mFileHandle = open( mPathToNmeaSource.c_str(), O_RDONLY | O_NOCTTY );
    if ( mFileHandle == -1 )
    {
        FWE_LOG_ERROR( "Could not open GPS NMEA file:" + mPathToNmeaSource );
        return false;
    }

    mThread = std::thread( [this]() {
        Thread::setCurrentThreadName( "IWaveGpsSource" );
        while ( !mShouldStop )
        {
            pollData();
            std::this_thread::sleep_for( std::chrono::milliseconds( mPollIntervalMs ) );
        }
    } );
    return true;
}

IWaveGpsSource::~IWaveGpsSource()
{
    if ( mThread.joinable() )
    {
        mShouldStop = true;
        mThread.join();
    }
    if ( close( mFileHandle ) != 0 )
    {
        FWE_LOG_ERROR( "Could not close NMEA file" );
    }
    mFileHandle = -1;
    // coverity[cert_err50_cpp_violation] false positive - join is called to exit the previous thread
    // coverity[autosar_cpp14_a15_5_2_violation] false positive - join is called to exit the previous thread
}

} // namespace IoTFleetWise
} // namespace Aws
