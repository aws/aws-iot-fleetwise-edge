
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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

    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mTriggerType, TriggerType::SIGNALVALUE );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mTriggerType, TriggerType::SIGNALVALUE );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[0].mValuePredicate.mCondition,
               PredicateCondition::LESS );
    ASSERT_EQ( parser.getCollectionScheme()->getEventTriggers()[1].mValuePredicate.mCondition,
               PredicateCondition::BIGGER );
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
    ASSERT_EQ( parser.getCollectionScheme()->getTimeTrigger().mValuePredicate.mCondition, PredicateCondition::EVERY );
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
