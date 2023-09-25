// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "GeohashInfo.h"
#include "ICollectionScheme.h"
#include <cstdint>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This is the Geohash Function Node module that can be used as part of the AST tree to
 * evaluate whether the Geohash has changed at given precision.
 *
 */
class GeohashFunctionNode
{
public:
    /**
     * @brief Default Destructor.
     */
    ~GeohashFunctionNode() = default;

    /**
     * @brief This function calculates current Geohash for the vehicle and compare it against
     * the previous Geohash.
     * The Geohash will always be calculated at the maximum precision defined in Geohash module.
     * However, the comparison between current and previous Geohash is at the precision specified at the input.
     *
     * For Instance: Geohash calculated previously at 9q9hwg28j and currently at 9q9hwheb9. If the precision is
     * specified as 5, the comparison would return EQUAL. If the precision is specified as 6, the comparison
     * would return NOT EQUAL.
     *
     * @param latitude latitude from GPS
     * @param longitude longitude from GPS
     * @param precision In Geohash, precision is the length of hash character.
     * @param gpsUnitType The GPS signal latitude / longitude unit type. The following unit type is supported:
     * 1) DECIMAL DEGREE 2) MICROARCSECOND 3) MILLIARCSECOND 4) ARCSECOND
     * @return True if geohash has changed at given precision. False if geohash has not changed.
     */
    bool evaluateGeohash( double latitude,
                          double longitude,
                          uint8_t precision,
                          GeohashFunction::GPSUnitType gpsUnitType );

    /**
     * @brief Consume the latest evaluated geohash info. This function will set the mIsGeohashNew
     * flag to false to avoid re-consuming the same geohash.
     */
    void consumeGeohash( GeohashInfo &geohashInfo );

    /**
     * @brief return whether a new Geohash has been generated but not read (consumed) yet.
     *
     * @return true if a new Geohash has been evaluated but not read yet. false if no new geohash.
     */
    bool hasNewGeohash() const;

private:
    /**
     * @brief Utility function to convert different GPS unit to Decimal Degree
     *
     * @return converted GPS signal in Decimal Degree
     */
    static double convertToDecimalDegree( double value, GeohashFunction::GPSUnitType gpsUnitType );

    /**
     * @brief This is the Geohash in String format holding the latest calculated geohash
     */
    GeohashInfo mGeohashInfo;

    /**
     * @brief If this flag is true, it indicates a Geohash has been generated but not read yet.
     */
    bool mIsGeohashNew{ false };
};

} // namespace IoTFleetWise
} // namespace Aws
