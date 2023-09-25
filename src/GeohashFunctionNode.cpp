// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "GeohashFunctionNode.h"
#include "Geohash.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

bool
GeohashFunctionNode::evaluateGeohash( double latitude,
                                      double longitude,
                                      uint8_t precision,
                                      GeohashFunction::GPSUnitType gpsUnitType )
{
    // set flag to false. It will turn true only if Geohash has been evaluated as changed at given precision.
    this->mIsGeohashNew = false;
    // Perform unit conversion to Decimal Degree which is the only format supported by Geohash library
    latitude = convertToDecimalDegree( latitude, gpsUnitType );
    longitude = convertToDecimalDegree( longitude, gpsUnitType );
    // If precision is out of bound, we limit it to max precision supported by Geohash library
    if ( precision > Geohash::MAX_PRECISION )
    {
        precision = Geohash::MAX_PRECISION;
    }
    std::string currentGeohashString{};
    if ( Geohash::encode( latitude, longitude, Geohash::MAX_PRECISION, currentGeohashString ) )
    {
        // FWE_LOG_INFO( "Geohash calculated: " + currentGeohashString );
        // First we want to make sure both geohash string has the valid format for comparison
        if ( ( this->mGeohashInfo.mGeohashString.length() >= precision ) &&
             ( currentGeohashString.length() >= precision ) )
        {
            // We compare the front part of string at given precision
            if ( currentGeohashString.substr( 0, precision ) !=
                 this->mGeohashInfo.mGeohashString.substr( 0, precision ) )
            {
                FWE_LOG_TRACE( "Geohash has changed from " + this->mGeohashInfo.mGeohashString + " to " +
                               currentGeohashString + " at given precision " + std::to_string( precision ) );
                this->mIsGeohashNew = true;
            }
        }
        else if ( !this->mGeohashInfo.hasItems() )
        {
            // There's no existing Geohash, set the flag to true. One use case is first time Geohash evaluation.
            this->mIsGeohashNew = true;
            FWE_LOG_TRACE( "Geohash start at: " + currentGeohashString );
        }
        else
        {
            TraceModule::get().incrementVariable( TraceVariable::GE_COMPARE_PRECISION_ERROR );
            FWE_LOG_ERROR( "Cannot compare two Geohashes as they have less precision than required" );
        }
        this->mGeohashInfo.mGeohashString = currentGeohashString;
    }
    else
    {
        TraceModule::get().incrementVariable( TraceVariable::GE_EVALUATE_ERROR_LAT_LON );
        FWE_LOG_ERROR( "Unable to calculate Geohash with lat/lon: " + std::to_string( latitude ) + ", " +
                       std::to_string( longitude ) );
    }
    return this->mIsGeohashNew;
}

void
GeohashFunctionNode::consumeGeohash( GeohashInfo &geohashInfo )
{
    // set the flag to be false as the geohash has been read out.
    this->mIsGeohashNew = false;
    geohashInfo = this->mGeohashInfo;
    this->mGeohashInfo.mPrevReportedGeohashString = this->mGeohashInfo.mGeohashString;
    FWE_LOG_TRACE( "Previous Geohash is updated to " + this->mGeohashInfo.mPrevReportedGeohashString );
}

bool
GeohashFunctionNode::hasNewGeohash() const
{
    return this->mIsGeohashNew;
}

double
GeohashFunctionNode::convertToDecimalDegree( double value, GeohashFunction::GPSUnitType gpsUnitType )
{
    double convertedValue = 0;
    switch ( gpsUnitType )
    {
    case GeohashFunction::GPSUnitType::DECIMAL_DEGREE:
        convertedValue = value;
        break;
    case GeohashFunction::GPSUnitType::MICROARCSECOND:
        convertedValue = value / 3600000000.0F;
        break;
    case GeohashFunction::GPSUnitType::MILLIARCSECOND:
        convertedValue = value / 3600000.0F;
        break;
    case GeohashFunction::GPSUnitType::ARCSECOND:
        convertedValue = value / 3600.0F;
        break;
    default:
        break;
    }
    return convertedValue;
}

} // namespace IoTFleetWise
} // namespace Aws
