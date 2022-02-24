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

#include "datatypes/CANRawMessage.h"
#include <gtest/gtest.h>

using namespace Aws::IoTFleetWise::VehicleNetwork;

TEST( CANRawMessageTest, SetupNormalMessage )
{
    // Test for a normal message.
    // The message should be set up correctly.
    CANRawMessage message;

    std::uint32_t id = 0xFFU;
    std::uint8_t dlc = 8U;
    const std::uint8_t frameData[8] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    timestampT timestamp = static_cast<timestampT>( 12345678 );

    ASSERT_TRUE( message.setup( id, dlc, frameData, timestamp ) );
    ASSERT_EQ( message.getMessageID(), id );
    ASSERT_TRUE( message.isValid() );
    ASSERT_EQ( message.getMessageDLC(), dlc );
    for ( std::uint32_t i = 0; i < dlc; i++ )
    {
        ASSERT_EQ( frameData[i], ( message.getMessageFrame() )[i] );
    }
    ASSERT_EQ( message.getReceptionTimestamp(), timestamp );
}

TEST( CANRawMessageTest, RejectZeroDlcTest )
{
    // Test for a message with zero DLC.
    // The message should be rejected.
    CANRawMessage message;

    std::uint32_t id = 0xFFU;
    std::uint8_t dlc = 0U;
    const std::uint8_t frameData[8] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    timestampT timestamp = static_cast<timestampT>( 12345678 );

    ASSERT_FALSE( message.setup( id, dlc, frameData, timestamp ) );
    ASSERT_FALSE( message.isValid() );
}
