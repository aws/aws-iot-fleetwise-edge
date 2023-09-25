// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Metadata representing an event of interest.
 * An event is an entity that represents a point in time when something has happened and
 * requires AWS IoT FleetWise to collect data for the period of time before and after its
 * occurrence. The Metadata consists of ( but not limited, and thus can be extended ) :
 *
 * @param eventID Unique identifier of the event as a string, unique across the system and
 * unique also after resets. The timestamp of this event itself is implicit and is equal to the
 * time of reception of this notification.
 * @param sourceID Unique identifier of the source from which the data should be collected.
 * This can be for example the source name ( e.g. CAN0  ), an IP address of a camera or
 * any other unique identifier that is known as a source of vehicle data across the system.
 * @param negativeOffsetMs amount of time in milliseconds before the event happened.
 * @param positiveOffsetMs amount of time in milliseconds after the event happened.
 */
struct EventMetadata

{
    EventMetadata() = default;
    EventMetadata( const uint32_t &evID, const uint32_t &srcID, const uint32_t &negOff, const uint32_t &posOff )
        : eventID( evID )
        , sourceID( srcID )
        , negativeOffsetMs( negOff )
        , positiveOffsetMs( posOff )
    {
    }
    uint32_t eventID{ 0 };
    uint32_t sourceID{ 0 };
    uint32_t negativeOffsetMs{ 0 };
    uint32_t positiveOffsetMs{ 0 };
};

class InspectionEventListener
{

public:
    virtual ~InspectionEventListener() = default;

    /**
     * @brief Notification raised when an event of interest is detected by the Inspection
     * Engine during the incoming data evaluation. During evaluation, the inspection engine uses
     * the inspection matrix metadata to extract the time span to be applied by this type of events
     * Listeners to this notification are free to utilise this notification in the way
     * it's appropriate i.e. this is a fire and forget. The timestamp of the event occurrence is implicit
     * and is the time of this notification.
     * @param eventMetadata vector of EventMetadata consisting of:
     *   - eventID unique identifier of the event ( persist across reboots )
     *   - negativeOffsetMs amount of time in milliseconds before the event occurrence.
     *   - positiveOffsetMs amount of time in milliseconds after the event occurrence.
     *   - sourceID this is the identifier of the device to which this event should be forwarded.
     */
    virtual void onEventOfInterestDetected( const std::vector<EventMetadata> &eventMetadata ) = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
