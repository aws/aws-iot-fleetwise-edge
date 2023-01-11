// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CollectionSchemeJSONParser.h"
#include "datatypes/VehicleDataSourceTypes.h"
#include <fstream>
#include <iostream>
#include <json/json.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

CollectionSchemeJSONParser::CollectionSchemeJSONParser( const std::string &path )
{
    mPath = path;
    mCollectionScheme.reset( new CollectionScheme() );
}

bool
CollectionSchemeJSONParser::parse()
{
    std::ifstream collectionSchemeFile( mPath, std::ifstream::in );
    if ( collectionSchemeFile.is_open() )
    {
        Json::Value root;
        Json::CharReaderBuilder builder;
        JSONCPP_STRING errs;
        bool success = parseFromStream( builder, collectionSchemeFile, &root, &errs );
        if ( !success )
        {
            mLogger.error( "CollectionSchemeJSONParser::parse", "Failed to parse the collectionScheme file: " + mPath );
            return false;
        }
        mCollectionScheme->setVersion( root["version"].asString() );
        mCollectionScheme->setCollectionSchemeID( root["collectionSchemeID"].asString() );
        mCollectionScheme->setEventType( root["eventType"].asString() );
        mCollectionScheme->setEventID( root["eventID"].asUInt64() );
        const auto eventTriggers = root["eventTriggers"];
        const auto eventMessages = root["eventMessages"];
        EventTriggers triggers;
        // Triggers
        for ( unsigned int i = 0; i < eventTriggers.size(); ++i )
        {
            EventTrigger trigger;
            // Trigger
            const std::string triggerType = eventTriggers[i]["triggerType"].asString();
            if ( triggerType == "timepoint" )
            {
                trigger.mTriggerType = TriggerType::TIMEPOINT;
            }
            else if ( triggerType == "signalvalue" )
            {
                trigger.mTriggerType = TriggerType::SIGNALVALUE;
            }
            // Predicate
            // Condition
            const std::string condition = eventTriggers[i]["valuePredicate"]["condition"].asString();
            if ( condition == "every" )
            {
                trigger.mValuePredicate.mCondition = PredicateCondition::EVERY;
            }
            else if ( condition == "equal" )
            {
                trigger.mValuePredicate.mCondition = PredicateCondition::EQUAL;
            }
            else if ( condition == "less" )
            {
                trigger.mValuePredicate.mCondition = PredicateCondition::LESS;
            }
            else if ( condition == "bigger" )
            {
                trigger.mValuePredicate.mCondition = PredicateCondition::BIGGER;
            }
            // Value
            trigger.mValuePredicate.mValue = eventTriggers[i]["valuePredicate"]["value"].asDouble();
            // SignalID
            if ( trigger.mTriggerType == TriggerType::SIGNALVALUE )
            {
                trigger.mSignalID = eventTriggers[i]["valuePredicate"]["signalID"].asUInt();
                trigger.mParentMessageID = eventTriggers[i]["valuePredicate"]["messageID"].asUInt();
                mCollectionScheme->insertSignalTriggerToMessage( trigger.mParentMessageID, trigger );
            }
            if ( trigger.mTriggerType == TriggerType::TIMEPOINT )
            {
                mCollectionScheme->setTimeTrigger( trigger );
            }
            else
            {
                triggers.emplace_back( trigger );
            }
        }
        mCollectionScheme->setEventTriggers( triggers );
        // Messages
        for ( unsigned int i = 0; i < eventMessages.size(); ++i )
        {
            CANMessageFormat message;
            message.mMessageID = eventMessages[i]["messageID"].asUInt();
            message.mSizeInBytes = static_cast<uint8_t>( eventMessages[i]["sizeInBytes"].asUInt() );
            message.mIsMultiplexed = eventMessages[i]["isMultiplexed"].asBool();
            const auto signals = eventMessages[i]["signals"];
            for ( unsigned int j = 0; j < signals.size(); ++j )
            {
                CANSignalFormat signal;
                signal.mSignalID = signals[j]["signalID"].asUInt();
                signal.mIsBigEndian = signals[j]["isBigEndian"].asBool();
                signal.mIsSigned = signals[j]["isSigned"].asBool();
                signal.mFirstBitPosition = static_cast<uint16_t>( signals[j]["firstBitPosition"].asUInt() );
                signal.mSizeInBits = static_cast<uint16_t>( signals[j]["sizeInBits"].asUInt() );
                signal.mOffset = signals[j]["offset"].asDouble();
                signal.mFactor = signals[j]["factor"].asDouble();
                signal.mIsMultiplexorSignal = signals[j]["isMultiplexer"].asBool();
                signal.mMultiplexorValue = static_cast<uint8_t>( signals[j]["multiplexerValue"].asUInt() );
                message.mSignals.emplace_back( signal );
            }
            mCollectionScheme->insertMessageToCollected( message.mMessageID, message );
        }

        collectionSchemeFile.close();
        return true;
    }
    else
    {
        mLogger.error( "CollectionSchemeJSONParser::parse", "Failed to open the collectionScheme file: " + mPath );
        return false;
    }
}

bool
CollectionSchemeJSONParser::reLoad()
{
    std::lock_guard<std::mutex> lock( mMutex );
    mCollectionScheme->clear();
    bool success = parse();
    return success;
}

CollectionSchemePtr
CollectionSchemeJSONParser::getCollectionScheme()
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mCollectionScheme;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
