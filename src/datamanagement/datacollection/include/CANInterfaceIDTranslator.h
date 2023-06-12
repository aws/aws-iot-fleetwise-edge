// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "SignalTypes.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

/**
 * @brief Translate the internal used Id to the Id used in config file and decoder manifest
 * Adding new items is not thread safe
 */
class CANInterfaceIDTranslator
{

public:
    void
    add( CANInterfaceID iid )
    {
        mLookup.emplace_back( mCounter, iid );
        mCounter++;
    }

    CANChannelNumericID
    getChannelNumericID( const CANInterfaceID &iid )
    {
        for ( auto l : mLookup )
        {
            if ( l.second == iid )
            {
                return l.first;
            }
        }
        return INVALID_CAN_SOURCE_NUMERIC_ID;
    };

    CANInterfaceID
    getInterfaceID( CANChannelNumericID cid )
    {
        for ( auto l : mLookup )
        {
            if ( l.first == cid )
            {
                return l.second;
            }
        }
        return INVALID_CAN_INTERFACE_ID;
    };

private:
    std::vector<std::pair<CANChannelNumericID, CANInterfaceID>> mLookup;
    CANChannelNumericID mCounter{ 0 };
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
