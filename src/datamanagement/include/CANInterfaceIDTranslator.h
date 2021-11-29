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

#include "IDecoderManifest.h"

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
    CANInterfaceIDTranslator()
        : mCounter( 0 )
    {
    }

    void
    add( CANInterfaceID iid )
    {
        mLookup.emplace_back( mCounter, iid );
        mCounter++;
    }

    CANChannelNumericID
    getChannelNumericID( CANInterfaceID iid )
    {
        for ( auto l : mLookup )
        {
            if ( l.second == iid )
            {
                return l.first;
            }
        }
        return INVALID_CAN_CHANNEL_NUMERIC_ID;
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
    CANChannelNumericID mCounter;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws