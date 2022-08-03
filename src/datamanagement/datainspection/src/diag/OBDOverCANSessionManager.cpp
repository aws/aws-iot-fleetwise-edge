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

// Includes
#include "OBDOverCANSessionManager.h"
#include "EnumUtility.h"
#include "OBDDataTypes.h"
#include "TraceModule.h"
#include <cstring>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::DataManagement;

bool
OBDOverCANSessionManager::init( const std::string &gatewayCanInterfaceName )
{
    // The rest of the options are left to their default value.
    // There is no frame control in this channel, as this is only for keep alive purposes.
    // Further requests to the Engine ECU happens in the main diagnostic module.
    ISOTPOverCANSenderReceiverOptions options(
        gatewayCanInterfaceName, toUType( ECUID::BROADCAST_ID ), toUType( ECUID::ENGINE_ECU_RX ) );
    // Initialize the request
    OBDRequest KeepAliveOBDRequest = { static_cast<SID>( 0x01 ), static_cast<PID>( 0x00 ) };
    mTxPDU.clear();
    mTxPDU.emplace_back( toUType( KeepAliveOBDRequest.mSID ) );
    mTxPDU.emplace_back( KeepAliveOBDRequest.mPID );
    // Initialize the channel
    return mSenderReceiver.init( options );
}

bool
OBDOverCANSessionManager::sendHeartBeat()
{

    if ( mSenderReceiver.sendPDU( mTxPDU ) )
    {
        mLogger.trace( "OBDOverCANSessionManager::doWork", " Keep Alive Request Sent" );
        return true;
    }
    else
    {
        TraceModule::get().incrementVariable( TraceVariable::OBD_KEEP_ALIVE_ERROR );
        mLogger.error( "OBDOverCANSessionManager::doWork", " Keep Alive Request Failed" );
        return false;
    }
}

bool
OBDOverCANSessionManager::connect()
{
    return mSenderReceiver.connect();
}

bool
OBDOverCANSessionManager::disconnect()
{
    return mSenderReceiver.disconnect();
}

bool
OBDOverCANSessionManager::isAlive()
{
    return mSenderReceiver.isAlive();
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws