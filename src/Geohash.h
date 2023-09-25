// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This is the Geohash module that can convert GPS latitude and longitude to GeoHash
 *
 * See more details about Geohash at wikipedia: https://en.wikipedia.org/wiki/Geohash
 *
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class Geohash
{
public:
    /**
     * @brief Encoding function that takes latitude and longitude and precision and output Geohash
     * in bits format.
     * Note the LSb is the last bit of Geohash. The unused bits are filled with 0.
     * @param lat: latitude in Decimal Degree
     * @param lon: longitude in Decimal Degree
     * @param precision: In Geohash, precision is the length of hash character.
     * @param hashBits: pass by reference. Function will set its value with the calculated Geohash
     * @return True if encode is successful, false is encode failed due to input
     */
    static bool encode( double lat, double lon, uint8_t precision, uint64_t &hashBits );
    /**
     * @brief Encoding function that takes latitude and longitude and precision and output Geohash
     * in String (base 32) format.
     * @param lat: latitude in Decimal Degree
     * @param lon: longitude in Decimal Degree
     * @param precision: In Geohash, precision is the length of hash character.
     * @param hashString: pass by reference. Function will set its value with the calculated Geohash
     * @return True if encode is successful, false is encode failed due to input
     */
    static bool encode( double lat, double lon, uint8_t precision, std::string &hashString );

    // In GeoHash, precision is specified by the length of hash. 9 characters hash can specify a
    // rectangle area of 4.77m X 4.77m. Here we defined the maximum precision as 9 characters.
    static constexpr uint8_t MAX_PRECISION = 9;

private:
    // number of bits in Base32 character
    static constexpr uint8_t BASE32_BITS = 5;
    // minimum Latitude
    static constexpr double LAT_MIN = -90.0;
    // maximum Latitude
    static constexpr double LAT_MAX = 90.0;
    // minimum Longitude
    static constexpr double LON_MIN = -180.0;
    // maximum Longitude
    static constexpr double LON_MAX = 180.0;
};

} // namespace IoTFleetWise
} // namespace Aws
