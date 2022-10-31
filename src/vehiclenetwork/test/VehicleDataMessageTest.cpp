// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "datatypes/VehicleDataMessage.h"
#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::Platform::Linux;

TEST( VehicleDataMessageTest, SetupTest )
{
    // Test for a normal message.
    // The message should be set up correctly.
    VehicleDataMessage message;

    std::uint32_t id = 0xFFU;
    const std::vector<std::uint8_t> rawData = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const std::vector<boost::any> syntheticData = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    Timestamp timestamp = static_cast<Timestamp>( 12345678 );
    message.setup( id, rawData, syntheticData, timestamp );
    ASSERT_EQ( message.getMessageID(), id );
    ASSERT_TRUE( message.isValid() );
    // Raw Data
    for ( size_t i = 0; i < rawData.size(); i++ )
    {
        ASSERT_EQ( rawData[i], ( message.getRawData() )[i] );
    }

    // Synthetic Data
    for ( size_t i = 0; i < syntheticData.size(); i++ )
    {
        ASSERT_EQ( boost::any_cast<uint>( syntheticData[i] ), boost::any_cast<uint>( message.getSyntheticData()[i] ) );
    }
    // Timestamp
    ASSERT_EQ( message.getReceptionTimestamp(), timestamp );
}
