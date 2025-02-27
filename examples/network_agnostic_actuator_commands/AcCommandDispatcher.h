// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <aws/iotfleetwise/ICommandDispatcher.h>
#include <aws/iotfleetwise/TimeTypes.h>
#include <string>
#include <vector>

class AcCommandDispatcher : public Aws::IoTFleetWise::ICommandDispatcher
{
public:
    AcCommandDispatcher();
    ~AcCommandDispatcher() override = default;

    AcCommandDispatcher( const AcCommandDispatcher & ) = delete;
    AcCommandDispatcher &operator=( const AcCommandDispatcher & ) = delete;
    AcCommandDispatcher( AcCommandDispatcher && ) = delete;
    AcCommandDispatcher &operator=( AcCommandDispatcher && ) = delete;

    /**
     * @brief Initializer command dispatcher with its associated underlying vehicle network / service
     * @return True if successful. False otherwise.
     */
    bool init() override;

    /**
     * @brief set actuator value
     * @param actuatorName Actuator name
     * @param signalValue Signal value
     * @param commandId Command ID
     * @param issuedTimestampMs Timestamp of when the command was issued in the cloud in ms since
     * epoch.
     * @param executionTimeoutMs Relative execution timeout in ms since `issuedTimestampMs`. A value
     * of zero means no timeout.
     * @param notifyStatusCallback Callback to notify command status
     */
    void setActuatorValue( const std::string &actuatorName,
                           const Aws::IoTFleetWise::SignalValueWrapper &signalValue,
                           const Aws::IoTFleetWise::CommandID &commandId,
                           Aws::IoTFleetWise::Timestamp issuedTimestampMs,
                           Aws::IoTFleetWise::Timestamp executionTimeoutMs,
                           Aws::IoTFleetWise::NotifyCommandStatusCallback notifyStatusCallback ) override;

    /**
     * @brief Gets the actuator names supported by the command dispatcher
     * @todo The decoder manifest doesn't yet have an indication of whether a signal is
     * READ/WRITE/READ_WRITE. Until it does this interface is needed to get the names of the
     * actuators supported by the command dispatcher, so that for string signals, buffers can be
     * pre-allocated in the RawDataManager by the CollectionSchemeManager when a new decoder
     * manifest arrives. When the READ/WRITE/READ_WRITE usage of a signal is available this
     * interface can be removed.
     * @return List of actuator names
     */
    std::vector<std::string> getActuatorNames() override;

private:
    std::string mActuatorName;
};
