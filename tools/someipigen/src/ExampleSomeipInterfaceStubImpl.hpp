// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "v1/commonapi/ExampleSomeipInterfaceStubDefault.hpp"
#include <atomic>
#include <boost/any.hpp>
#include <boost/any/bad_any_cast.hpp>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstdint> // IWYU pragma: keep
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

/**
 * @brief Represents a signal
 */
struct Signal
{
    boost::any value;
    std::function<void( void )> onChange;

    unsigned responseDelayMsForGetMethod{ 0 };
    unsigned responseDelayMsForSetMethod{ 0 };

    Signal() = default;
    Signal( boost::any initialValue, std::function<void( void )> onChangeIn = nullptr )
        : value( std::move( initialValue ) )
        , onChange( std::move( onChangeIn ) )
    {
    }
};

/**
 * @brief Implementation of the ExampleSomeipInterface service stub.
 *
 * This class provides the implementation for the ExampleSomeipInterface service, including
 * methods to get and set properties as well as handling client requests.
 */
class ExampleSomeipInterfaceStubImpl : public v1_0::commonapi::ExampleSomeipInterfaceStubDefault
{
public:
    ExampleSomeipInterfaceStubImpl( boost::asio::io_context &ioContext );

    virtual ~ExampleSomeipInterfaceStubImpl() = default;

    /* Simulated long running command handler that waits asynchronously with a
    period of 1 second for 10 seconds and sends periodic notification on
    progress of execution to FWE. */
    template <typename T>
    void
    setValueLRCAsyncHandler( const boost::system::error_code &error,
                             unsigned timerId,
                             boost::asio::steady_timer *t,
                             T value,
                             Signal *signal,
                             const std::string &commandID,
                             int32_t commandStatus,
                             int32_t commandReasonCode,
                             int32_t commandStatusData,
                             std::function<void()> callback )
    {
        if ( commandStatusData == 100 || error == boost::asio::error::operation_aborted )
        {
            std::lock_guard<std::mutex> lock( mTimersMutex );
            mTimers.erase( timerId );
        }

        if ( error == boost::asio::error::operation_aborted )
        {
            return;
        }

        /* After 10 seconds update/set the value. */
        if ( commandStatusData == 100 )
        {
            signal->value = boost::any( value );
            callback();
        }
        else
        {

            /* Send periodic notification to dispatcher with progress information. */
            fireNotifyLRCStatusEvent(
                commandID, commandStatus, commandReasonCode, std::to_string( commandStatusData ) + " percent" );

            /* Increment progress by 10% */
            commandStatusData += 10;

            /* Async wait for 1 second */
            t->expires_after( std::chrono::milliseconds( 1000 ) );
            t->async_wait( std::bind( &ExampleSomeipInterfaceStubImpl::setValueLRCAsyncHandler<T>,
                                      this,
                                      boost::asio::placeholders::error,
                                      timerId,
                                      t,
                                      value,
                                      signal,
                                      commandID,
                                      commandStatus,
                                      commandReasonCode,
                                      commandStatusData,
                                      callback ) );
        }
    }

    /* Sets value, but takes 10 seconds to complete
    this function is to simulate long running commands that sends periodic
    broadcast notification with command progress over SOME/IP. */
    template <typename T>
    void
    setValueLRCAsync( const std::string &commandID,
                      const std::string &signalName,
                      T value,
                      std::function<void()> callback )
    {

        auto it = mSignals.find( signalName );

        std::lock_guard<std::mutex> lock( mTimersMutex );
        auto timerId = ++mTimerId;
        auto &timer = mTimers.emplace( timerId, boost::asio::steady_timer( mIoContext ) ).first->second;

        int32_t commandStatus = 10;    /* CommandStatus::IN_PROGRESS */
        int32_t commandReasonCode = 0; /* Reason code REASON_CODE_UNSPECIFIED */
        int32_t commandStatusData = 0;

        auto bindedHandler = std::bind( &ExampleSomeipInterfaceStubImpl::setValueLRCAsyncHandler<T>,
                                        this,
                                        boost::asio::placeholders::error,
                                        timerId,
                                        &timer,
                                        value,
                                        &it->second,
                                        commandID,
                                        commandStatus,
                                        commandReasonCode,
                                        commandStatusData,
                                        callback );

        timer.expires_after( std::chrono::milliseconds( it->second.responseDelayMsForSetMethod ) );
        timer.async_wait( bindedHandler );
    }

    template <typename T>
    void
    setValueAsync( const std::string &signalName, T value, std::function<void()> callback )
    {
        auto it = mSignals.find( signalName );

        std::lock_guard<std::mutex> lock( mTimersMutex );
        auto timerId = ++mTimerId;
        auto &timer = mTimers.emplace( timerId, boost::asio::steady_timer( mIoContext ) ).first->second;
        timer.expires_after( std::chrono::milliseconds( it->second.responseDelayMsForSetMethod ) );
        auto signal = &it->second;
        timer.async_wait( [this, timerId, value, signal, callback]( const boost::system::error_code &error ) {
            {
                std::lock_guard<std::mutex> lock( mTimersMutex );
                mTimers.erase( timerId );
            }

            if ( error == boost::asio::error::operation_aborted )
            {
                return;
            }
            signal->value = boost::any( value );
            callback();
        } );
    }

    template <typename T>
    void
    getValueAsync( const std::string &signalName, std::function<void( T value )> callback )
    {
        auto it = mSignals.find( signalName );

        std::lock_guard<std::mutex> lock( mTimersMutex );
        auto timerId = ++mTimerId;
        auto &timer = mTimers.emplace( timerId, boost::asio::steady_timer( mIoContext ) ).first->second;
        timer.expires_after( std::chrono::milliseconds( it->second.responseDelayMsForGetMethod ) );
        auto signal = &it->second;
        timer.async_wait( [this, timerId, signal, callback, signalName]( const boost::system::error_code &error ) {
            {
                std::lock_guard<std::mutex> lock( mTimersMutex );
                mTimers.erase( timerId );
            }

            if ( error == boost::asio::error::operation_aborted )
            {
                return;
            }

            try
            {
                callback( boost::any_cast<T>( signal->value ) );
            }
            catch ( const boost::bad_any_cast &e )
            {
                std::cerr << "Type mismatch for " << signalName << " " << e.what() << "\n";
            }
        } );
    }

    /**
     * @brief Sets the value of the Int32 property.
     *
     * @param client The client making the request.
     * @param value The new value for the Int32 property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setInt32( const std::shared_ptr<CommonAPI::ClientId> client,
                           int32_t value,
                           setInt32Reply_t reply ) override;

    /**
     * @brief Sets the value of the Int32 property but simulate long running
     * command.
     *
     * @param client The client making the request.
     * @param commandId Command ID from the dispatcher.
     * @param value The new value for the Int32 property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setInt32LongRunning( const std::shared_ptr<CommonAPI::ClientId> client,
                                      std::string commandId,
                                      int32_t value,
                                      setInt32LongRunningReply_t reply ) override;

    /**
     * @brief Retrieves the value of the Int32 property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getInt32( const std::shared_ptr<CommonAPI::ClientId> client, getInt32Reply_t reply ) override;

    /**
     * @brief Retrieves the value of the Int32 property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getInt32LongRunning( const std::shared_ptr<CommonAPI::ClientId> client,
                                      getInt32Reply_t reply ) override;

    /**
     * @brief Sets the value of the Int64 property.
     *
     * @param client The client making the request.
     * @param value The new value for the property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setInt64( const std::shared_ptr<CommonAPI::ClientId> client,
                           int64_t value,
                           setInt64Reply_t reply ) override;

    /**
     * @brief Retrieves the value of the Int64 property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getInt64( const std::shared_ptr<CommonAPI::ClientId> client, getInt64Reply_t reply ) override;

    /**
     * @brief Sets the value of the Boolean property.
     *
     * @param client The client making the request.
     * @param value The new value for the property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setBoolean( const std::shared_ptr<CommonAPI::ClientId> client,
                             bool value,
                             setBooleanReply_t reply ) override;

    /**
     * @brief Retrieves the value of the Boolean property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getBoolean( const std::shared_ptr<CommonAPI::ClientId> client, getBooleanReply_t reply ) override;

    /**
     * @brief Sets the value of the Float property.
     *
     * @param client The client making the request.
     * @param value The new value for the property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setFloat( const std::shared_ptr<CommonAPI::ClientId> client,
                           float value,
                           setFloatReply_t reply ) override;

    /**
     * @brief Retrieves the value of the Float property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getFloat( const std::shared_ptr<CommonAPI::ClientId> client, getFloatReply_t reply ) override;

    /**
     * @brief Sets the value of the Double property.
     *
     * @param client The client making the request.
     * @param value The new value for the property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setDouble( const std::shared_ptr<CommonAPI::ClientId> client,
                            double value,
                            setDoubleReply_t reply ) override;

    /**
     * @brief Retrieves the value of the Double property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getDouble( const std::shared_ptr<CommonAPI::ClientId> client, getDoubleReply_t reply ) override;

    /**
     * @brief Sets the value of the String property.
     *
     * @param client The client making the request.
     * @param value The new value for the property.
     * @param reply The reply callback to confirm the operation.
     */
    virtual void setString( const std::shared_ptr<CommonAPI::ClientId> client,
                            std::string value,
                            setStringReply_t reply ) override;

    /**
     * @brief Retrieves the value of the String property.
     *
     * @param client The client making the request.
     * @param reply The reply callback to provide the value.
     */
    virtual void getString( const std::shared_ptr<CommonAPI::ClientId> client, getStringReply_t reply ) override;

    /**
     * @brief Accessor for the signals map.
     *
     * @return A constant reference to the map of signal names to Signal structs.
     */
    std::map<std::string, Signal> &
    getSignals()
    {
        return mSignals;
    }

private:
    std::map<std::string, Signal> mSignals;
    boost::asio::io_context &mIoContext;

    // container to hold the async timers, otherwise if a timer is destructed, the callback is
    // immediately called because it timer is aborted.
    std::unordered_map<unsigned, boost::asio::steady_timer> mTimers;
    std::mutex mTimersMutex;
    std::atomic<unsigned> mTimerId{ 0 };
};
