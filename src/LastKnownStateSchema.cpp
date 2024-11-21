// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LastKnownStateSchema.h"
#include "LoggingModule.h"
#include "TraceModule.h"

namespace Aws
{
namespace IoTFleetWise
{

LastKnownStateSchema::LastKnownStateSchema( std::shared_ptr<IReceiver> receiverLastKnownState )
{
    receiverLastKnownState->subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onLastKnownStateReceived( receivedMessage );
    } );
}

void
LastKnownStateSchema::onLastKnownStateReceived( const ReceivedConnectivityMessage &receivedMessage )
{
    auto lastKnownStateIngestion = std::make_shared<LastKnownStateIngestion>();

    if ( !lastKnownStateIngestion->copyData( receivedMessage.buf, receivedMessage.size ) )
    {
        FWE_LOG_ERROR( "LastKnownState copyData from IoT core failed" );
        return;
    }

    mLastKnownStateListeners.notify( lastKnownStateIngestion );
    FWE_LOG_TRACE( "Received state templates" );
    TraceModule::get().incrementVariable( TraceVariable::STATE_TEMPLATES_RECEIVED );
}

} // namespace IoTFleetWise
} // namespace Aws
