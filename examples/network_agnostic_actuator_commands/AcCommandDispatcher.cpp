// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AcCommandDispatcher.h"
#include <aws/iotfleetwise/LoggingModule.h>
#include <functional>

AcCommandDispatcher::AcCommandDispatcher()
{
}

bool
AcCommandDispatcher::init()
{
    return true;
}

void
AcCommandDispatcher::setActuatorValue( const std::string &actuatorName,
                                       const Aws::IoTFleetWise::SignalValueWrapper &signalValue,
                                       const Aws::IoTFleetWise::CommandID &commandId,
                                       Aws::IoTFleetWise::Timestamp issuedTimestampMs,
                                       Aws::IoTFleetWise::Timestamp executionTimeoutMs,
                                       Aws::IoTFleetWise::NotifyCommandStatusCallback notifyStatusCallback )
{
    // Here invoke your actuation
    FWE_LOG_INFO( "Actuator " + actuatorName + " executed successfully for command ID " + commandId );
    notifyStatusCallback( Aws::IoTFleetWise::CommandStatus::SUCCEEDED, 0x1234, "Success" );
}

std::vector<std::string>
AcCommandDispatcher::getActuatorNames()
{
    return { "Vehicle.ActivateAC" };
}
