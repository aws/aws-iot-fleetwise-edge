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
