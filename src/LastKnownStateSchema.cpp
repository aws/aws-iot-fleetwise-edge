// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateSchema.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <memory>

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
    auto lastKnownStateIngestion = std::make_unique<LastKnownStateIngestion>();

    if ( !lastKnownStateIngestion->copyData( receivedMessage.buf, receivedMessage.size ) )
    {
        FWE_LOG_ERROR( "LastKnownState copyData from IoT core failed" );
        return;
    }

    // coverity[autosar_cpp14_a20_8_6_violation] can't use make_shared as unique_ptr is moved
    mLastKnownStateListeners.notify( std::shared_ptr<LastKnownStateIngestion>( std::move( lastKnownStateIngestion ) ) );
    FWE_LOG_TRACE( "Received state templates" );
    TraceModule::get().incrementVariable( TraceVariable::STATE_TEMPLATES_RECEIVED );
}

} // namespace IoTFleetWise
} // namespace Aws
