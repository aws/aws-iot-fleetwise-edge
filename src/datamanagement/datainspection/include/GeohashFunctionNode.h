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

#pragma once

#include "Geohash.h"
#include "GeohashInfo.h"
#include "ICollectionScheme.h"
#include "LoggingModule.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::DataManagement;

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
     * @param lat: latitude from GPS
     * @param lon: longitude from GPS
     * @param precision: In Geohash, precision is the length of hash character.
     * @param gpsUnitType: The GPS signal latitude / longitude unit type. The following unit type is supported:
     * 1) DECIMAL DEGREE 2) MICROARCSECOND 3) MILLIARCSECOND 4) ARCSECOND
     * @return True if geohash has changed at given precision. False if geohash has not changed.
     */
    bool evaluateGeohash( double lat, double lon, uint8_t precision, GeohashFunction::GPSUnitType gpsUnitType );

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
    static double convertToDecimalDegree( double val, GeohashFunction::GPSUnitType gpsUnitType );

    /**
     * @brief This is the Geohash in String format holding the latest calculated geohash
     */
    GeohashInfo mGeohashInfo;

    /**
     * @brief If this flag is true, it indicates a Geohash has been generated but not read yet.
     */
    bool mIsGeohashNew{ false };

    /**
     * @brief Logging module used to output to logs
     */
    LoggingModule mLogger;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws