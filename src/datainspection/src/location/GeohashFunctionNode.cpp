/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

// Includes
#include "GeohashFunctionNode.h"
#include "TraceModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

GeohashFunctionNode::GeohashFunctionNode()
    : mGeohashInfo()
    , mIsGeohashNew( false )
{
}

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
        // mLogger.info( "GeohashFunctionNode::evaluateGeohash", "Geohash calculated: " + currentGeohashString );
        // First we want to make sure both geohash string has the valid format for comparison
        if ( this->mGeohashInfo.mGeohashString.length() >= precision && currentGeohashString.length() >= precision )
        {
            // We compare the front part of string at given precision
            if ( currentGeohashString.substr( 0, precision ) !=
                 this->mGeohashInfo.mGeohashString.substr( 0, precision ) )
            {
                mLogger.trace( "GeohashFunctionNode::evaluateGeohash",
                               "Geohash has changed from " + this->mGeohashInfo.mGeohashString + " to " +
                                   currentGeohashString + " at given precision " + std::to_string( precision ) );
                this->mIsGeohashNew = true;
            }
        }
        else if ( !this->mGeohashInfo.hasItems() )
        {
            // There's no existing Geohash, set the flag to true. One use case is first time Geohash evaluation.
            this->mIsGeohashNew = true;
            mLogger.trace( "GeohashFunctionNode::evaluateGeohash", "Geohash start at: " + currentGeohashString );
        }
        else
        {
            TraceModule::get().incrementVariable( TraceVariable::GE_COMPARE_PRECISION_ERROR );
            mLogger.error( "GeohashFunctionNode::evaluateGeohash",
                           "Cannot compare two Geohashes as they have less precision than required" );
        }
        this->mGeohashInfo.mGeohashString = currentGeohashString;
    }
    else
    {
        TraceModule::get().incrementVariable( TraceVariable::GE_EVALUATE_ERROR_LAT_LON );
        mLogger.error( "GeohashFunctionNode::evaluateGeohash",
                       "Unable to calculate Geohash with lat/lon: " + std::to_string( latitude ) + ", " +
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
    mLogger.trace( "GeohashFunctionNode::consumeGeohash ",
                   "Previous Geohash is updated to " + this->mGeohashInfo.mPrevReportedGeohashString );
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
        convertedValue = value / 3600000000.0f;
        break;
    case GeohashFunction::GPSUnitType::MILLIARCSECOND:
        convertedValue = value / 3600000.0f;
        break;
    case GeohashFunction::GPSUnitType::ARCSECOND:
        convertedValue = value / 3600.0f;
        break;
    default:
        break;
    }
    return convertedValue;
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
