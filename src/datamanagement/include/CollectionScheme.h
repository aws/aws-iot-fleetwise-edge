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

#pragma once

// Includes
#include "CANDataTypes.h"
#include "IDecoderManifest.h"
#include <map>
#include <memory>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
/**
 * @brief A set of structs used to represent the AWS IoT FleetWise Collection Scheme.
 */
enum TriggerType
{
    TIMEPOINT,
    SIGNALVALUE
};

enum PredicateCondition
{
    EVERY,
    EQUAL,
    LESS,
    BIGGER
};

struct ValuePredicate
{
    ValuePredicate()
        : mCondition( EVERY )
        , mValue( 0.0 )
    {
    }

    PredicateCondition mCondition;
    double mValue;
};

struct EventTrigger
{
    EventTrigger()
        : mTriggerType( TIMEPOINT )
        , mSignalID( 0x0 )
        , mParentMessageID( 0x0 )
    {
    }

    TriggerType mTriggerType;
    uint32_t mSignalID;
    uint32_t mParentMessageID;
    ValuePredicate mValuePredicate;
    bool
    isValid() const
    {
        return ( mTriggerType == SIGNALVALUE && mSignalID > 0 && mValuePredicate.mCondition != EVERY ) ||
               ( mTriggerType == TIMEPOINT && mValuePredicate.mCondition == EVERY && mValuePredicate.mValue > 0.0 );
    }
    bool
    operator<( const EventTrigger &a ) const
    {
        return mSignalID < a.mSignalID;
    }
};

typedef std::vector<EventTrigger> EventTriggers;
typedef std::map<uint32_t, CANMessageFormat> CollectedCANMessages;
typedef std::map<uint32_t, EventTriggers> WatchedCANMessages;

class CollectionScheme
{

public:
    CollectionScheme();
    ~CollectionScheme();
    bool isValid();
    void setVersion( const std::string &version );
    void setCollectionSchemeID( const std::string &collectionSchemeID );
    void setEventType( const std::string &eventType );
    void setTimeTrigger( const EventTrigger &timeTrigger );
    void setEventTriggers( const EventTriggers &eventTriggers );
    void setEventID( const uint64_t &eventID );
    const std::string &getVersion() const;
    const std::string &getCollectionSchemeID() const;
    const std::string &getEventType() const;
    const uint64_t &getEventID() const;
    const EventTriggers &getEventTriggers() const;
    const EventTrigger &getTimeTrigger() const;

    void insertMessageToBeWatched( const uint32_t &messageID, const EventTriggers &triggers );
    void insertMessageToCollected( const uint32_t &messageID, const CANMessageFormat &messageFormat );
    void insertSignalTriggerToMessage( const uint32_t &messageID, const EventTrigger &trigger );
    void clear();
    const WatchedCANMessages &getWatchedCANMessages() const;
    const CollectedCANMessages &getCollectedCANMessages() const;
    bool shouldWatchCANMessage( const uint32_t &messageID ) const;
    const CANMessageFormat &getCANMessageFormat( const uint32_t &messageID ) const;
    bool shouldCollectCANMessage( const uint32_t &messageID ) const;
    bool shouldTriggerUpload( const CANFrameInfo &frameInfo, EventTrigger &eventTrigger );

private:
    std::string mVersion;
    std::string mCollectionSchemeID;
    std::string mEventType;
    uint64_t mEventID;
    EventTriggers mEventTriggers;
    CollectedCANMessages mMessageIDsToBeCollected;
    WatchedCANMessages mMessageIDsToBeWatched;
    EventTrigger mTimeTrigger;
};
typedef std::shared_ptr<CollectionScheme> CollectionSchemePtr;

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws