// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CollectionScheme.h"
#include <algorithm>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

bool
CollectionScheme::isValid()
{
    return ( ( !mMessageIDsToBeWatched.empty() ) && ( !mEventTriggers.empty() ) ) ||
           ( ( !mMessageIDsToBeCollected.empty() ) && ( mTimeTrigger.isValid() ) );
}

void
CollectionScheme::clear()
{
    mVersion = "";
    mCollectionSchemeID = "";
    mEventType = "";
    mEventID = 0;

    mMessageIDsToBeCollected.clear();
    mMessageIDsToBeWatched.clear();
}

void
CollectionScheme::insertMessageToCollected( const uint32_t &messageID, const CANMessageFormat &messageFormat )
{
    mMessageIDsToBeCollected.emplace( messageID, messageFormat );
}
void
CollectionScheme::insertSignalTriggerToMessage( const uint32_t &messageID, const EventTrigger &trigger )
{
    if ( !trigger.isValid() )
    {
        return;
    }
    auto it = mMessageIDsToBeWatched.find( messageID );
    if ( it != mMessageIDsToBeWatched.end() )
    {
        // Append the trigger to the message if it exists.
        it->second.emplace_back( trigger );
        // Sort the triggers to allow fast lookup during evaluation.
        std::sort( it->second.begin(), it->second.end() );
    }
    else
    {
        // First item in the trigger
        EventTriggers triggers;
        triggers.emplace_back( trigger );

        mMessageIDsToBeWatched.emplace( messageID, triggers );
    }
}

void
CollectionScheme::setVersion( const std::string &version )
{
    mVersion = version;
}

void
CollectionScheme::setCollectionSchemeID( const std::string &collectionSchemeID )
{
    mCollectionSchemeID = collectionSchemeID;
}
void
CollectionScheme::setEventType( const std::string &eventType )
{
    mEventType = eventType;
}
void
CollectionScheme::setEventID( const uint64_t &eventID )
{
    mEventID = eventID;
}

void
CollectionScheme::setTimeTrigger( const EventTrigger &timeTrigger )
{
    if ( !timeTrigger.isValid() )
    {
        return;
    }

    mTimeTrigger = timeTrigger;
}

void
CollectionScheme::setEventTriggers( const EventTriggers &eventTriggers )
{
    mEventTriggers.assign( eventTriggers.begin(), eventTriggers.end() );
}

const std::string &
CollectionScheme::getVersion() const
{
    return mVersion;
}

const std::string &
CollectionScheme::getCollectionSchemeID() const
{
    return mCollectionSchemeID;
}

const std::string &
CollectionScheme::getEventType() const
{
    return mEventType;
}

const uint64_t &
CollectionScheme::getEventID() const
{
    return mEventID;
}

const EventTriggers &
CollectionScheme::getEventTriggers() const
{
    return mEventTriggers;
}
const EventTrigger &
CollectionScheme::getTimeTrigger() const
{
    return mTimeTrigger;
}

const WatchedCANMessages &
CollectionScheme::getWatchedCANMessages() const
{
    return mMessageIDsToBeWatched;
}
const CollectedCANMessages &
CollectionScheme::getCollectedCANMessages() const
{
    return mMessageIDsToBeCollected;
}

bool
CollectionScheme::shouldWatchCANMessage( const uint32_t &messageID ) const
{
    auto it = mMessageIDsToBeWatched.find( messageID );
    return it != mMessageIDsToBeWatched.end() ? true : false;
}

const CANMessageFormat &
CollectionScheme::getCANMessageFormat( const uint32_t &messageID ) const
{
    static CANMessageFormat format;
    auto it = mMessageIDsToBeCollected.find( messageID );
    if ( it != mMessageIDsToBeCollected.end() )
    {
        return it->second;
    }
    return format;
}

bool
CollectionScheme::shouldCollectCANMessage( const uint32_t &messageID ) const
{
    auto it = mMessageIDsToBeCollected.find( messageID );
    return it != mMessageIDsToBeCollected.end() ? true : false;
}

bool
CollectionScheme::shouldTriggerUpload( const CANFrameInfo &frameInfo, EventTrigger &eventTrigger )
{
    bool predicateMet = false;
    auto it = mMessageIDsToBeWatched.find( frameInfo.mFrameID );
    if ( it != mMessageIDsToBeWatched.end() )
    {
        // Lookup the triggers for this message
        // the triggers are already sorted by signalID during parsing.
        size_t i = 0;
        while ( ( i < frameInfo.mSignals.size() ) )
        {
            for ( size_t j = 0; j < it->second.size(); ++j )
            {
                if ( ( it->second[j].mTriggerType == TriggerType::SIGNALVALUE ) &&
                     ( it->second[j].mSignalID == frameInfo.mSignals[i].mSignalID ) )
                {
                    if ( it->second[j].mValuePredicate.mCondition == PredicateCondition::LESS )
                    {
                        predicateMet = frameInfo.mSignals[i].mPhysicalValue < it->second[j].mValuePredicate.mValue;
                    }
                    else if ( it->second[j].mValuePredicate.mCondition == PredicateCondition::BIGGER )
                    {
                        predicateMet = frameInfo.mSignals[i].mPhysicalValue > it->second[j].mValuePredicate.mValue;
                    }
                    else if ( it->second[j].mValuePredicate.mCondition == PredicateCondition::EQUAL )
                    {
                        predicateMet = frameInfo.mSignals[i].mPhysicalValue == it->second[j].mValuePredicate.mValue;
                    }
                }
                if ( predicateMet )
                {
                    // Hint at the trigger
                    eventTrigger = it->second[j];
                    return predicateMet;
                }
            }
            i++;
        }
    }
    return predicateMet;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
