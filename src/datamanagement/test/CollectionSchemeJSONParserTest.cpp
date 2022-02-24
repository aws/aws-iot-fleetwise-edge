
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

#include "CollectionSchemeJSONParser.h"
#include <gtest/gtest.h>
#include <unistd.h>

using namespace Aws::IoTFleetWise::DataManagement;

TEST( CollectionSchemeJSONParserTest, CollectionSchemeJSONParserSignalTrigger )
{
    CollectionSchemeJSONParser parser( "dm-collection-scheme-example.json" );
    ASSERT_TRUE( parser.parse() );
    ASSERT_EQ( parser.getCollectionScheme()->getVersion(), "1.0" );
    ASSERT_EQ( parser.getCollectionScheme()->getCollectionSchemeID(), "arn::testCollectionScheme" );
    ASSERT_EQ( parser.getCollectionScheme()->getEventType(), "heartbeat" );
    ASSERT_EQ( parser.getCollectionScheme()->getCollectedCANMessages().size(), 3 );
    ASSERT_EQ( parser.getCollectionScheme()->getWatchedCANMessages().size(), 2 );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers().size(), 2 );

    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mTriggerType, SIGNALVALUE );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mTriggerType, SIGNALVALUE );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mValuePredicate.mCondition, LESS );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mValuePredicate.mCondition, BIGGER );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mValuePredicate.mValue, 1 );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mValuePredicate.mValue, 1 );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mSignalID, 10 );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mSignalID, 30 );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mParentMessageID, 123 );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mParentMessageID, 456 );
    CANMessageFormat f123 = parser.getCollectionScheme()->getCANMessageFormat( 123 );
    CANMessageFormat f456 = parser.getCollectionScheme()->getCANMessageFormat( 456 );
    ASSERT_TRUE( f123.isValid() );
    ASSERT_EQ( f123.mSignals.size(), 2 );
    ASSERT_TRUE( f456.isValid() );
    ASSERT_EQ( f456.mSignals.size(), 2 );
    ASSERT_TRUE( f123 != f456 );
}

TEST( CollectionSchemeJSONParserTest, CollectionSchemeJSONParserTimeTrigger )
{
    CollectionSchemeJSONParser parser( "dm-collection-scheme-example.json" );
    ASSERT_TRUE( parser.parse() );
    ASSERT_TRUE( parser.getCollectionScheme()->getTimeTrigger().isValid() );
    ASSERT_EQ( parser.getCollectionScheme()->getTimeTrigger().mValuePredicate.mCondition, EVERY );
    ASSERT_EQ( parser.getCollectionScheme()->getTimeTrigger().mValuePredicate.mValue, 10 );
}

TEST( CollectionSchemeJSONParserTest, CollectionSchemeJSONParserMultiplexer )
{
    CollectionSchemeJSONParser parser( "dm-collection-scheme-example.json" );
    ASSERT_TRUE( parser.parse() );
    ASSERT_EQ( parser.getCollectionScheme()->getCollectedCANMessages().size(), 3 );
    auto it = parser.getCollectionScheme()->getCollectedCANMessages().find( 678 );
    ASSERT_TRUE( it->second.isMultiplexed() );
}
