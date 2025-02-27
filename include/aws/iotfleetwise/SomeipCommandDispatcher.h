// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/ISomeipInterfaceWrapper.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <CommonAPI/CommonAPI.hpp>
#include <memory>
#include <string>
#include <vector>

#if !defined( COMMONAPI_INTERNAL_COMPILATION )
#define COMMONAPI_INTERNAL_COMPILATION
#define HAS_DEFINED_COMMONAPI_INTERNAL_COMPILATION_HERE
#endif
#include <CommonAPI/Proxy.hpp>
#ifdef HAS_DEFINED_COMMONAPI_INTERNAL_COMPILATION_HERE
// coverity[misra_cpp_2008_rule_16_0_3_violation] Required to workaround CommonAPI include mechanism
#undef COMMONAPI_INTERNAL_COMPILATION
// coverity[misra_cpp_2008_rule_16_0_3_violation] Required to workaround CommonAPI include mechanism
#undef HAS_DEFINED_COMMONAPI_INTERNAL_COMPILATION_HERE
#endif

namespace Aws
{
namespace IoTFleetWise
{

/**
 * This class implements interface ICommandDispatcher. It's a generic class for dispatching command
 * onto SOME/IP. It accepts a SOME/IP interface specific wrapper to dispatch command to the corresponding
 * SOME/IP interface
 */
class SomeipCommandDispatcher : public ICommandDispatcher
{
public:
    SomeipCommandDispatcher( std::shared_ptr<ISomeipInterfaceWrapper> someipInterfaceWrapper );

    ~SomeipCommandDispatcher() override = default;

    SomeipCommandDispatcher( const SomeipCommandDispatcher & ) = delete;
    SomeipCommandDispatcher &operator=( const SomeipCommandDispatcher & ) = delete;
    SomeipCommandDispatcher( SomeipCommandDispatcher && ) = delete;
    SomeipCommandDispatcher &operator=( SomeipCommandDispatcher && ) = delete;

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
                           const SignalValueWrapper &signalValue,
                           const CommandID &commandId,
                           Timestamp issuedTimestampMs,
                           Timestamp executionTimeoutMs,
                           NotifyCommandStatusCallback notifyStatusCallback ) override;

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
    /**
     * @brief The shared pointer to the base class commonAPI proxy
     */
    std::shared_ptr<CommonAPI::Proxy> mProxy;

    /**
     * @brief The shared pointer to the interface specific wrapper
     */
    std::shared_ptr<ISomeipInterfaceWrapper> mSomeipInterfaceWrapper;
};

} // namespace IoTFleetWise
} // namespace Aws
