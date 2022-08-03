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
#include "MessageTypes.h"
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
enum class TriggerType
{
    TIMEPOINT,
    SIGNALVALUE
};

enum class PredicateCondition
{
    EVERY,
    EQUAL,
    LESS,
    BIGGER
};

struct ValuePredicate
{
    PredicateCondition mCondition{ PredicateCondition::EVERY };
    double mValue{ 0.0 };
};

struct EventTrigger
{
    TriggerType mTriggerType{ TriggerType::TIMEPOINT };
    uint32_t mSignalID{ 0x0 };
    uint32_t mParentMessageID{ 0x0 };
    ValuePredicate mValuePredicate;
    bool
    isValid() const
    {
        return ( mTriggerType == TriggerType::SIGNALVALUE && mSignalID > 0 &&
                 mValuePredicate.mCondition != PredicateCondition::EVERY ) ||
               ( mTriggerType == TriggerType::TIMEPOINT && mValuePredicate.mCondition == PredicateCondition::EVERY &&
                 mValuePredicate.mValue > 0.0 );
    }
    bool
    operator<( const EventTrigger &a ) const
    {
        return mSignalID < a.mSignalID;
    }
};

using EventTriggers = std::vector<EventTrigger>;
using CollectedCANMessages = std::map<uint32_t, CANMessageFormat>;
using WatchedCANMessages = std::map<uint32_t, EventTriggers>;

class CollectionScheme
{

public:
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
    static constexpr uint64_t INVALID_EVENT_ID = 0;
    std::string mVersion;
    std::string mCollectionSchemeID;
    std::string mEventType;
    uint64_t mEventID{ INVALID_EVENT_ID };
    EventTriggers mEventTriggers;
    CollectedCANMessages mMessageIDsToBeCollected;
    WatchedCANMessages mMessageIDsToBeWatched;
    EventTrigger mTimeTrigger;
};
using CollectionSchemePtr = std::shared_ptr<CollectionScheme>;

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws