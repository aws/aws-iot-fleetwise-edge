// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateSchema.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TraceModule.h"

namespace Aws
{
namespace IoTFleetWise
{

LastKnownStateSchema::LastKnownStateSchema( IReceiver &receiverLastKnownState )
{
    receiverLastKnownState.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
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
