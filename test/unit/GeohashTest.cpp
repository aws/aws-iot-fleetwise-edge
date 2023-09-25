// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Geohash.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/** @brief test aims to check Geohash Encoding function to handle invalid
 *  input such as invalid lat/lon and precision.
 */
TEST( GeohashTest, GeohashEncodeWithInvalidInput )
{
    std::string hash;
    // Invalid lat provided to encode function
    ASSERT_FALSE( Geohash::encode( -91, -4.329004883, 9, hash ) );
    ASSERT_FALSE( Geohash::encode( 91, -4.329004883, 9, hash ) );
    // Invalid lon provided to encode function
    ASSERT_FALSE( Geohash::encode( -23, -432.9004883, 9, hash ) );
    ASSERT_FALSE( Geohash::encode( -23, 432.9004883, 9, hash ) );
    // Invalid precision provided to encode function
    ASSERT_FALSE( Geohash::encode( -23, -4.329004883, 10, hash ) );

    uint64_t hashBits = 0;
    // Invalid lat provided to encode function
    ASSERT_FALSE( Geohash::encode( -91, -4.329004883, 9, hashBits ) );
    ASSERT_FALSE( Geohash::encode( 91, -4.329004883, 9, hashBits ) );
    // Invalid lon provided to encode function
    ASSERT_FALSE( Geohash::encode( -23, -432.9004883, 9, hashBits ) );
    ASSERT_FALSE( Geohash::encode( -23, 432.9004883, 9, hashBits ) );
    // Invalid precision provided to encode function
    ASSERT_FALSE( Geohash::encode( -23, -4.329004883, 10, hashBits ) );
}

/** @brief This test aims to check Geohash encoding function to correctly
 *  encode GPS lat/lon to GeoHash.
 */
TEST( GeohashTest, GeohashEncodeWithValidInput )
{
    std::string hash;
    ASSERT_TRUE( Geohash::encode( 37.371392, -122.046208, 9, hash ) );
    ASSERT_EQ( hash, "9q9hwg28j" );

    ASSERT_TRUE( Geohash::encode( 37.371392, -122.046208, 5, hash ) );
    ASSERT_EQ( hash, "9q9hw" );

    ASSERT_TRUE( Geohash::encode( 47.620623, -122.348920, 6, hash ) );
    ASSERT_EQ( hash, "c22yzv" );

    ASSERT_TRUE( Geohash::encode( 0, 0, 9, hash ) );
    ASSERT_EQ( hash, "s00000000" );

    ASSERT_TRUE( Geohash::encode( -90.0, -180.0, 7, hash ) );
    ASSERT_EQ( hash, "0000000" );

    ASSERT_TRUE( Geohash::encode( 90, 180, 8, hash ) );
    ASSERT_EQ( hash, "zzzzzzzz" );

    uint64_t hashBits = 0;
    ASSERT_TRUE( Geohash::encode( 37.371392, -122.046208, 9, hashBits ) );
    ASSERT_EQ( hashBits, 10661749295377 );

    ASSERT_TRUE( Geohash::encode( 37.371392, -122.046208, 5, hashBits ) );
    ASSERT_EQ( hashBits, 10167836 );

    ASSERT_TRUE( Geohash::encode( 47.620623, -122.348920, 6, hashBits ) );
    ASSERT_EQ( hashBits, 371293179 );

    ASSERT_TRUE( Geohash::encode( 0, 0, 9, hashBits ) );
    ASSERT_EQ( hashBits, 26388279066624 );

    ASSERT_TRUE( Geohash::encode( -90.0, -180.0, 7, hashBits ) );
    ASSERT_EQ( hashBits, 0 );

    ASSERT_TRUE( Geohash::encode( 90, 180, 8, hashBits ) );
    ASSERT_EQ( hashBits, 1099511627775 );
}

} // namespace IoTFleetWise
} // namespace Aws
