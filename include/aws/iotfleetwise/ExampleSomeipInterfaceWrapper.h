// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ISomeipInterfaceWrapper.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "v1/commonapi/ExampleSomeipInterfaceProxy.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <boost/optional/optional.hpp>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * This class is the wrapper class for the example SOME/IP interface. It's responsible for building
 * SOME/IP interface proxy and expose the proxy as CommonAPI::Proxy base class shared pointer. The
 * SOME/IP method is encapsulated in a method wrapper with unified input signature.
 *
 * To support custom SOME/IP interface:
 * 1. Create a new wrapper class by copying this class
 * 2. Update the class name with the new interface name
 * 3. Update the Proxy class name with the new interface proxy name
 * 4. Create a new method wrapper to encapsulate the method call. Take referenceMethodWrapper as example
 * 5. Update mSupportedActuatorInfo with a new entry containing the actuator name, signal type and
 *    method wrapper function name
 */
class ExampleSomeipInterfaceWrapper : public ISomeipInterfaceWrapper
{
public:
    ExampleSomeipInterfaceWrapper( std::string domain,
                                   std::string instance,
                                   std::string connection,
                                   std::function<std::shared_ptr<v1::commonapi::ExampleSomeipInterfaceProxy<>>(
                                       std::string, std::string, std::string )> buildProxy,
                                   RawData::BufferManager *rawDataBufferManager,
                                   bool subscribeToLongRunningCommandStatus )
        : mDomain( std::move( domain ) )
        , mInstance( std::move( instance ) )
        , mConnection( std::move( connection ) )
        , mBuildProxy( std::move( buildProxy ) )
        , mRawDataBufferManager( rawDataBufferManager )
        , mSubscribeToLongRunningCommandStatus( subscribeToLongRunningCommandStatus )
        , mSupportedActuatorInfo( {
              { "Vehicle.actuator1",
                { SignalType::INT32,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper1(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
              { "Vehicle.actuator2",
                { SignalType::INT64,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper2(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
              { "Vehicle.actuator3",
                { SignalType::BOOLEAN,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper3(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
              { "Vehicle.actuator4",
                { SignalType::FLOAT,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper4(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
              { "Vehicle.actuator5",
                { SignalType::DOUBLE,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper5(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
              { "Vehicle.actuator9",
                { SignalType::STRING,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper9(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
              { "Vehicle.actuator20",
                { SignalType::INT32,
                  [this]( auto signalValue,
                          auto commandId,
                          auto issuedTimestampMs,
                          auto executionTimeoutMs,
                          auto notifyStatusCallback ) {
                      referenceMethodWrapper20(
                          signalValue, commandId, issuedTimestampMs, executionTimeoutMs, notifyStatusCallback );
                  } } },
          } )
    {
    }
    ~ExampleSomeipInterfaceWrapper() override
    {
        if ( mLongRunningCommandStatusSubscription.has_value() )
        {
            mProxy->getNotifyLRCStatusEvent().unsubscribe( *mLongRunningCommandStatusSubscription );
        }
    };

    ExampleSomeipInterfaceWrapper( const ExampleSomeipInterfaceWrapper & ) = delete;
    ExampleSomeipInterfaceWrapper &operator=( const ExampleSomeipInterfaceWrapper & ) = delete;
    ExampleSomeipInterfaceWrapper( ExampleSomeipInterfaceWrapper && ) = delete;
    ExampleSomeipInterfaceWrapper &operator=( ExampleSomeipInterfaceWrapper && ) = delete;

    bool
    init() override
    {
        mProxy = mBuildProxy( mDomain, mInstance, mConnection );
        if ( mProxy == nullptr )
        {
            FWE_LOG_ERROR( "Failed to build proxy " );
            return false;
        }

        if ( mSubscribeToLongRunningCommandStatus )
        {
            /* Subscribe to the long running command broadcast messages with
            the given lambda as the handler.*/
            mLongRunningCommandStatusSubscription = mProxy->getNotifyLRCStatusEvent().subscribe(
                // coverity[autosar_cpp14_a5_1_9_violation] This lambda isn't duplicated anywhere
                [this]( const std::string &commandID,
                        const int32_t &commandStatus,
                        const int32_t &commandReasonCode,
                        const std::string &commandReasonDescription ) {
                    std::lock_guard<std::mutex> lock( mCommandIDToNotifyCallbackMapMutex );
                    auto commandCallback = mCommandIDToNotifyCallbackMap.find( commandID );
                    if ( commandCallback == mCommandIDToNotifyCallbackMap.end() )
                    {
                        FWE_LOG_ERROR( "Unknown command ID: " + commandID );
                        return;
                    }

                    // coverity[autosar_cpp14_a7_2_1_violation] Basic checking is performed above
                    CommandStatus cmdStatus = static_cast<CommandStatus>( commandStatus );
                    CommandReasonCode cmdReasonCode = static_cast<CommandReasonCode>( commandReasonCode );

                    commandCallback->second( cmdStatus, cmdReasonCode, commandReasonDescription );
                } );
        }

        return true;
    }

    std::shared_ptr<CommonAPI::Proxy>
    getProxy() const override
    {
        return std::static_pointer_cast<CommonAPI::Proxy>( mProxy );
    }

    // Create one method wrapper for each SOME/IP method
    void
    referenceMethodWrapper1( SignalValueWrapper signalValue,
                             const CommandID &commandId,
                             Timestamp issuedTimestampMs,
                             Timestamp executionTimeoutMs,
                             NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        FWE_LOG_TRACE( "set actuator value to " + std::to_string( signalValue.value.int32Val ) + " for command ID " +
                       commandId );
        mProxy->setInt32Async(
            signalValue.value.int32Val,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
            },
            &info );
    }

    void
    referenceMethodWrapper2( SignalValueWrapper signalValue,
                             const CommandID &commandId,
                             Timestamp issuedTimestampMs,
                             Timestamp executionTimeoutMs,
                             NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        FWE_LOG_TRACE( "set actuator value to " + std::to_string( signalValue.value.int64Val ) + " for command ID " +
                       commandId );
        mProxy->setInt64Async(
            signalValue.value.int64Val,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
            },
            &info );
    }

    void
    referenceMethodWrapper3( SignalValueWrapper signalValue,
                             const CommandID &commandId,
                             Timestamp issuedTimestampMs,
                             Timestamp executionTimeoutMs,
                             NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        FWE_LOG_TRACE( "set actuator value to " + std::to_string( signalValue.value.boolVal ) + " for command ID " +
                       commandId );
        mProxy->setBooleanAsync(
            signalValue.value.boolVal,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
            },
            &info );
    }

    void
    referenceMethodWrapper4( SignalValueWrapper signalValue,
                             const CommandID &commandId,
                             Timestamp issuedTimestampMs,
                             Timestamp executionTimeoutMs,
                             NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        FWE_LOG_TRACE( "set actuator value to " + std::to_string( signalValue.value.floatVal ) + " for command ID " +
                       commandId );
        mProxy->setFloatAsync(
            signalValue.value.floatVal,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
            },
            &info );
    }

    void
    referenceMethodWrapper5( SignalValueWrapper signalValue,
                             const CommandID &commandId,
                             Timestamp issuedTimestampMs,
                             Timestamp executionTimeoutMs,
                             NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        FWE_LOG_TRACE( "set actuator value to " + std::to_string( signalValue.value.doubleVal ) + " for command ID " +
                       commandId );
        mProxy->setDoubleAsync(
            signalValue.value.doubleVal,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
            },
            &info );
    }

    void
    referenceMethodWrapper9( SignalValueWrapper signalValue,
                             const CommandID &commandId,
                             Timestamp issuedTimestampMs,
                             Timestamp executionTimeoutMs,
                             NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        auto loanedFrame = mRawDataBufferManager->borrowFrame( signalValue.value.rawDataVal.signalId,
                                                               signalValue.value.rawDataVal.handle );
        if ( loanedFrame.isNull() )
        {
            notifyStatusCallback( CommandStatus::EXECUTION_FAILED, REASON_CODE_REJECTED, "" );
            return;
        }
        std::string stringVal;
        stringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
        FWE_LOG_TRACE( "set actuator value to " + stringVal + " for command ID " + commandId );
        mProxy->setStringAsync(
            stringVal,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
            },
            &info );
    }

    void
    referenceMethodWrapper20( SignalValueWrapper signalValue,
                              const CommandID &commandId,
                              Timestamp issuedTimestampMs,
                              Timestamp executionTimeoutMs,
                              NotifyCommandStatusCallback notifyStatusCallback )
    {
        CommonAPI::CallInfo info( commonapiGetRemainingTimeout( issuedTimestampMs, executionTimeoutMs ) );
        {
            std::lock_guard<std::mutex> lock( mCommandIDToNotifyCallbackMapMutex );
            auto result = mCommandIDToNotifyCallbackMap.emplace( commandId, notifyStatusCallback );
            if ( !result.second )
            {
                FWE_LOG_ERROR( "Duplicate command ID: " + commandId );
                return;
            }
        }
        FWE_LOG_TRACE( "set actuator value to " + std::to_string( signalValue.value.int32Val ) + " for command ID " +
                       commandId );
        mProxy->setInt32LongRunningAsync(
            commandId,
            signalValue.value.int32Val,
            // coverity[autosar_cpp14_a5_1_9_violation] Local variables need to be captured, so cannot be made common
            [this, commandId, notifyStatusCallback]( const CommonAPI::CallStatus &callStatus ) {
                notifyStatusCallback( commonapiCallStatusToCommandStatus( callStatus ),
                                      commonapiCallStatusToReasonCode( callStatus ),
                                      commonapiCallStatusToString( callStatus ) );
                std::lock_guard<std::mutex> lock( mCommandIDToNotifyCallbackMapMutex );
                mCommandIDToNotifyCallbackMap.erase( commandId );
            },
            &info );
    }

    const std::unordered_map<std::string, SomeipMethodInfo> &
    getSupportedActuatorInfo() const override
    {
        return mSupportedActuatorInfo;
    }

private:
    std::shared_ptr<v1::commonapi::ExampleSomeipInterfaceProxy<>> mProxy;
    std::string mDomain;
    std::string mInstance;
    std::string mConnection;
    std::function<std::shared_ptr<v1::commonapi::ExampleSomeipInterfaceProxy<>>(
        std::string, std::string, std::string )>
        mBuildProxy;
    RawData::BufferManager *mRawDataBufferManager;
    bool mSubscribeToLongRunningCommandStatus;
    boost::optional<v1::commonapi::ExampleSomeipInterfaceProxyBase::NotifyLRCStatusEvent::Subscription>
        mLongRunningCommandStatusSubscription;
    std::unordered_map<std::string, SomeipMethodInfo> mSupportedActuatorInfo;
    // Mutex to ensure atomic insertion to the command ID to notification callback map
    std::mutex mCommandIDToNotifyCallbackMapMutex;
    std::unordered_map<std::string, NotifyCommandStatusCallback> mCommandIDToNotifyCallbackMap;
};

} // namespace IoTFleetWise
} // namespace Aws
