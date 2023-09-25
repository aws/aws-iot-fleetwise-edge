// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Geohash.h"
namespace Aws
{
namespace IoTFleetWise
{

// This is a Base 32 character map. The map index is the number.
// For instance, 8 correspond to '8', 31 correspond to 'z'
static constexpr char base32Map[32] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'b',
                                        'c', 'd', 'e', 'f', 'g', 'h', 'j', 'k', 'm', 'n', 'p',
                                        'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z' };

bool
Geohash::encode( double lat, double lon, uint8_t precision, uint64_t &hashBits )
{
    // check MAX_PRECISION and BASE32_BITS validity at compile time.
    static_assert( sizeof( hashBits ) * 8 >= MAX_PRECISION * BASE32_BITS,
                   "Not enough bits to support maximum precision" );

    if ( ( precision > MAX_PRECISION ) || ( lat < LAT_MIN ) || ( lat > LAT_MAX ) || ( lon < LON_MIN ) ||
         ( lon > LON_MAX ) )
    {
        // INVALID INPUT, need to return as we cannot proceed for calculation
        return false;
    }
    auto latLow = LAT_MIN;
    auto latHigh = LAT_MAX;
    auto lonLow = LON_MIN;
    auto lonHigh = LON_MAX;
    // This is the Geohash in bits format. It's an uint64_t.
    hashBits = 0;
    // This is a flag indicating whether current bit represent longitude or latitude.
    // In Geohash, bits in odd position represent longitude, bits in even position represent latitude.
    // Note first bit is odd position, hence we set the initial flag to true.
    bool isLonBit = true;
    // Each hash character is in base 32 format, hence 5-bit. The total number bits in hash is
    // precision * 5
    int numOfHashBits = precision * BASE32_BITS;
    // Iterate through the bits
    for ( int i = 0; i < numOfHashBits; ++i )
    {
        if ( isLonBit )
        {
            // Binary Search on longitude
            auto lonMid = lonLow + ( lonHigh - lonLow ) / 2;
            if ( lon >= lonMid )
            {
                // Set the new bit to 1
                hashBits = ( hashBits << 1 ) | 1;
                lonLow = lonMid;
            }
            else
            {
                // Set the new bit to 0
                hashBits = ( hashBits << 1 ) | 0;
                lonHigh = lonMid;
            }
        }
        else
        {
            // Binary Search on latitude
            auto latMid = latLow + ( latHigh - latLow ) / 2;
            if ( lat >= latMid )
            {
                // Set the new bit to 1
                hashBits = ( hashBits << 1 ) | 1;
                latLow = latMid;
            }
            else
            {
                // Set the new bit to 0
                hashBits = ( hashBits << 1 ) | 0;
                latHigh = latMid;
            }
        }
        // Toggle the flag
        isLonBit = !isLonBit;
    }
    return true;
}

bool
Geohash::encode( double lat, double lon, uint8_t precision, std::string &hashString )
{
    if ( ( precision > MAX_PRECISION ) || ( lat < LAT_MIN ) || ( lat > LAT_MAX ) || ( lon < LON_MIN ) ||
         ( lon > LON_MAX ) )
    {
        // INVALID INPUT, need to return as we cannot proceed for calculation
        return false;
    }
    hashString = "";
    uint64_t hashBits = 0;
    // First we get the Geohash in raw bits format.
    encode( lat, lon, precision, hashBits );
    for ( uint8_t i = 0; i < precision; ++i )
    {
        // we iterate the hash bits from left to right
        // Each time we grab the 5-bit
        uint32_t base32Num = ( hashBits >> ( precision - 1 - i ) * BASE32_BITS ) & 0x1F;
        // Convert 5-bit to base 32 format
        hashString.append( 1, base32Map[base32Num] );
    }
    return true;
}

} // namespace IoTFleetWise
} // namespace Aws
