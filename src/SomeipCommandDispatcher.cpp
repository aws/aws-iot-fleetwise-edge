// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SomeipCommandDispatcher.h"
#include "LoggingModule.h"
#include <CommonAPI/CommonAPI.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <unordered_map>
#include <utility>

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

SomeipCommandDispatcher::SomeipCommandDispatcher( std::shared_ptr<ISomeipInterfaceWrapper> someipInterfaceWrapper )
    : mSomeipInterfaceWrapper( std::move( someipInterfaceWrapper ) )
{
}

std::vector<std::string>
SomeipCommandDispatcher::getActuatorNames()
{
    std::vector<std::string> actuatorNames;
    for ( const auto &config : mSomeipInterfaceWrapper->getSupportedActuatorInfo() )
    {
        actuatorNames.push_back( config.first );
    }
    return actuatorNames;
}

bool
SomeipCommandDispatcher::init()
{
    if ( ( mSomeipInterfaceWrapper == nullptr ) || ( !mSomeipInterfaceWrapper->init() ) )
    {
        FWE_LOG_ERROR( "Failed to initiate SOME/IP proxy" );
        return false;
    }

    mProxy = mSomeipInterfaceWrapper->getProxy();

    // Wait for up to 2 seconds for the proxy to become available.
    // This allows a command to be received immediately after MQTT connection to be executed,
    // which is what happens if the command was started while FWE was not running.
    std::promise<void> availabilityPromise;
    auto availabilitySubscription =
        mProxy->getProxyStatusEvent().subscribe( [&availabilityPromise]( CommonAPI::AvailabilityStatus status ) {
            if ( status == CommonAPI::AvailabilityStatus::AVAILABLE )
            {
                availabilityPromise.set_value();
            }
        } );
    auto res = availabilityPromise.get_future().wait_for( std::chrono::seconds( 2 ) );
    mProxy->getProxyStatusEvent().unsubscribe( availabilitySubscription );
    if ( res == std::future_status::timeout )
    {
        FWE_LOG_WARN( "Proxy currently unavailable" );
        // Don't return false, proxy may become available later
    }
    else
    {
        FWE_LOG_INFO( "Successfully initiated SOME/IP proxy" );
    }
    return true;
}

void
SomeipCommandDispatcher::setActuatorValue( const std::string &actuatorName,
                                           const SignalValueWrapper &signalValue,
                                           const CommandID &commandId,
                                           Timestamp issuedTimestampMs,
                                           Timestamp executionTimeoutMs,
                                           NotifyCommandStatusCallback notifyStatusCallback )
{
    // Sanity check to ensure proxy is valid
    if ( mProxy == nullptr )
    {
        auto reasonDescription = "Null proxy";
        FWE_LOG_ERROR( reasonDescription );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_INTERNAL_ERROR, reasonDescription );
        return;
    }
    // First let's check proxy is available
    if ( !mProxy->isAvailable() )
    {
        std::string reasonDescription = "Proxy unavailable";
        FWE_LOG_ERROR( reasonDescription + " for actuator " + actuatorName + " and command ID " + commandId );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_UNAVAILABLE, reasonDescription );
        return;
    }
    // Check whether actuator is supported
    auto it = mSomeipInterfaceWrapper->getSupportedActuatorInfo().find( actuatorName );
    if ( it == mSomeipInterfaceWrapper->getSupportedActuatorInfo().end() )
    {
        FWE_LOG_WARN( "Actuator " + actuatorName + " not supported for command ID " + commandId );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_NOT_SUPPORTED, "" );
        return;
    }
    // Check whether actuator value type matches with the supported method
    if ( signalValue.type != it->second.signalType )
    {
        FWE_LOG_ERROR( "Actuator " + actuatorName +
                       "'s value type mismatches with the supported value type for command ID " + commandId );
        notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_ARGUMENT_TYPE_MISMATCH, "" );
        return;
    }
    // Invoke the actual method via the method wrapper
    it->second.methodWrapper(
        signalValue,
        commandId,
        issuedTimestampMs,
        executionTimeoutMs,
        [commandId, notifyStatusCallback, actuatorName](
            CommandStatus status, CommandReasonCode reasonCode, const CommandReasonDescription &reasonDescription ) {
            std::string msg = "Actuator " + actuatorName + " response with command ID: " + commandId +
                              ", status: " + commandStatusToString( status ) +
                              ", reason code: " + std::to_string( reasonCode ) +
                              ", reason description: " + reasonDescription;
            if ( ( status == CommandStatus::IN_PROGRESS ) || ( status == CommandStatus::SUCCEEDED ) )
            {
                FWE_LOG_INFO( msg );
            }
            else
            {
                FWE_LOG_ERROR( msg );
            }
            notifyStatusCallback( status, reasonCode, reasonDescription );
        } );
}

} // namespace IoTFleetWise
} // namespace Aws
