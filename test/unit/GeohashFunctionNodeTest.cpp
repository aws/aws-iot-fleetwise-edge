// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "GeohashFunctionNode.h"
#include "Geohash.h"
#include "GeohashInfo.h"
#include "ICollectionScheme.h"
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{

// This is a utility function to convert GPS signal from Decimal Degree to input unit type
static double
convertFromDecimalDegreeTo( double value, GeohashFunction::GPSUnitType gpsUnitType )
{
    double convertedValue = 0;
    switch ( gpsUnitType )
    {
    case GeohashFunction::GPSUnitType::DECIMAL_DEGREE:
        convertedValue = value;
        break;
    case GeohashFunction::GPSUnitType::MICROARCSECOND:
        convertedValue = value * 3600000000;
        break;
    case GeohashFunction::GPSUnitType::MILLIARCSECOND:
        convertedValue = value * 3600000;
        break;
    case GeohashFunction::GPSUnitType::ARCSECOND:
        convertedValue = value * 3600;
        break;
    default:
        break;
    }
    return convertedValue;
}

/** @brief This test aims to check Geohash function node to correctly
 * evaluate geohash changes based on precision
 */
TEST( GeohashFunctionNodeTest, EvaluateGeohashChangeWithDifferentPrecision )
{
    GeohashFunctionNode geohashFunctionNode;
    GeohashInfo geohashInfo;
    // Start at Geohash "9q9hwg28j"
    double lat = 37.371392;
    double lon = -122.046208;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hwg28j" );
    // There was no geohash reported to the cloud, so the mPrevReportedGeohashString should be empty
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString.length(), 0 );

    // Changing longitude by 0.1 degree to set Geohash to "9q9hs7rb5"
    lon -= 0.1;
    // With precision 4, Geohash is the same "9q9h". So we expect evaluate function return false
    ASSERT_FALSE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    // There was no geohash reported to the cloud, so the mPrevReportedGeohashString should be empty
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString.length(), 0 );
    // Expect No Geohash update
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );

    // Changing longitude by 0.1 degree to set Geohash to "9q9hd5r01"
    lon -= 0.1;
    // With precision 5, Geohash tile has changed from "9q9hs" to "9q9hd"
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 5, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hd5r00" );
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString, "9q9hwg28j" );
}

/** @brief This test aims to check Geohash function node to report
 * no new Geohash if Geohash has been read out previously.
 */
TEST( GeohashFunctionNodeTest, hasNewGeohashReturnFalseIfAlreadyRead )
{
    GeohashFunctionNode geohashFunctionNode;
    GeohashInfo geohashInfo;
    // Start at Geohash "9q9hwg28j"
    double lat = 37.371392;
    double lon = -122.046208;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hwg28j" );
    // Since Geohash has been read out in last statement, query hasNewGeohash will return false
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );
}

/** @brief This test aims to check Geohash function node to gracefully
 *  handle corner case if GPS Signal is out of bound
 */
TEST( GeohashFunctionNodeTest, EvaluateGeohashWithOutOfRangeGPSSignal )
{
    GeohashFunctionNode geohashFunctionNode;
    // Start at Geohash "9q9hwg28j"
    double lat = 137.371392;
    double lon = -122.046208;
    // lat is not valid, hence evaluation cannot be done which will return false
    ASSERT_FALSE( geohashFunctionNode.evaluateGeohash( lat, lon, 5, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );

    // Changing longitude by 0.1 degree to set Geohash to "9q9hs7rb5"
    lat = 37.371392;
    lon = -222.046208;
    // lon is not valid, hence evaluation cannot be done which will return false
    ASSERT_FALSE( geohashFunctionNode.evaluateGeohash( lat, lon, 5, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );
}

/** @brief This test aims to check Geohash function node to gracefully
 *  handle corner case is precision is out of bound
 */
TEST( GeohashFunctionNodeTest, EvaluateGeohashWithOutOfRangePrecision )
{
    // Start up
    GeohashFunctionNode geohashFunctionNode;
    GeohashInfo geohashInfo;
    // Start at Geohash "9q9hwg28j"
    double lat = 37.371392;
    double lon = -122.046208;
    // if precision was set to 0. We shall expect evaluation return false as 0 precision is not valid for evaluation
    ASSERT_FALSE( geohashFunctionNode.evaluateGeohash( lat, lon, 0, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );

    // Changing longitude by 0.1 degree to set Geohash to "9q9hs7rb5"
    lon -= 0.1;
    // if precision was greater than maximum precision Geohash library can support. We will use maximum precision for
    // evaluation
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash(
        lat, lon, Geohash::MAX_PRECISION + 1, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hs7rb5" );
}

/** @brief This test aims to check Geohash function node to correctly evaluate that Geohash
 *  has not changed if car has not moved.
 */
TEST( GeohashFunctionNodeTest, EvaluateGeohashWithNoLocationChange )
{
    // Start up
    GeohashFunctionNode geohashFunctionNode;
    GeohashInfo geohashInfo;
    // Start at Geohash "9q9hwg28j"
    double lat = 37.371392;
    double lon = -122.046208;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hwg28j" );

    // Without no location change, we shall NOT see an geohash update
    ASSERT_FALSE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    // Expect No Geohash update
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );
}

/** @brief This test aims to check Geohash function node to correctly
 *  take GPS signals with different unit types
 */
TEST( GeohashFunctionNodeTest, EvaluateGeohashWithDifferentGPSUnitType )
{
    GeohashFunctionNode geohashFunctionNode;
    GeohashInfo geohashInfo;
    // Start at Geohash "9q9hwg28j"
    double lat = 37.371392;
    double lon = -122.046208;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hwg28j" );

    // Increase latitude by 0.1 degree to set Geohash to "9q9jnv2wt"
    lat += 0.1;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash(
        convertFromDecimalDegreeTo( lat, GeohashFunction::GPSUnitType::MICROARCSECOND ),
        convertFromDecimalDegreeTo( lon, GeohashFunction::GPSUnitType::MICROARCSECOND ),
        4,
        GeohashFunction::GPSUnitType::MICROARCSECOND ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9jnv2wt" );

    // Increase latitude by 0.2 degree to set Geohash to "9q9nqcbev"
    lat += 0.2;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash(
        convertFromDecimalDegreeTo( lat, GeohashFunction::GPSUnitType::MILLIARCSECOND ),
        convertFromDecimalDegreeTo( lon, GeohashFunction::GPSUnitType::MILLIARCSECOND ),
        4,
        GeohashFunction::GPSUnitType::MILLIARCSECOND ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9nqcbev" );

    // Increase latitude by 0.2 degree to set Geohash to "9q9pqy28v"
    lat += 0.2;
    ASSERT_TRUE(
        geohashFunctionNode.evaluateGeohash( convertFromDecimalDegreeTo( lat, GeohashFunction::GPSUnitType::ARCSECOND ),
                                             convertFromDecimalDegreeTo( lon, GeohashFunction::GPSUnitType::ARCSECOND ),
                                             4,
                                             GeohashFunction::GPSUnitType::ARCSECOND ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9pqy28v" );
}

/** This test aims to check previous Geohash function node to correctly
 *  evaluate geohash changes based on precision.
 */
TEST( GeohashFunctionNodeTest, EvaluatePreviousGeohashChange )
{
    GeohashFunctionNode geohashFunctionNode;
    GeohashInfo geohashInfo;
    // Start at Geohash "9q9hwg28j"
    double lat = 37.371392;
    double lon = -122.046208;
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 5, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hwg28j" );
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString.length(), 0 );

    // Changing longitude by 0.2 degree to set Geohash to "9q9hd5r00"
    lon -= 0.2;
    // With precision 5, previous geohash should update to "9q9hwg28j"
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 5, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hd5r00" );
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString, "9q9hwg28j" );

    // Changing longitude by 0.1 degree to set Geohash to "9q9hs7rb5"
    lon += 0.1;
    // With precision 5, previous geohash should update to "9q9hd5r00"
    ASSERT_TRUE( geohashFunctionNode.evaluateGeohash( lat, lon, 5, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_TRUE( geohashFunctionNode.hasNewGeohash() );
    geohashFunctionNode.consumeGeohash( geohashInfo );
    ASSERT_EQ( geohashInfo.mGeohashString, "9q9hs7rb5" );
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString, "9q9hd5r00" );

    // Changing longitude by 0.1 degree to set Geohash to "9q9hwg28j"
    lon += 0.1;
    // With precision 4, Geohash is the same "9q9h". So we expect evaluate function return false
    ASSERT_FALSE( geohashFunctionNode.evaluateGeohash( lat, lon, 4, GeohashFunction::GPSUnitType::DECIMAL_DEGREE ) );
    ASSERT_EQ( geohashInfo.mPrevReportedGeohashString, "9q9hd5r00" );
    // Expect No Geohash update
    ASSERT_FALSE( geohashFunctionNode.hasNewGeohash() );
}

} // namespace IoTFleetWise
} // namespace Aws
