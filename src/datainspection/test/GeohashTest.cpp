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

#include "Geohash.h"
#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::DataInspection;

/** @brief test aims to check Geohash Encoding function to handle invalid
 *  input such as invalid lat/lon and precision.
 */
TEST( GeohashTest, GeohashEncodeWithInvalidInput )
{
    std::string hash = "";
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
    std::string hash = "";
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
