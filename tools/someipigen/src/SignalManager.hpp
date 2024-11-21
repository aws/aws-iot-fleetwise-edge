// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ExampleSomeipInterfaceStubImpl.hpp"
#include <boost/any.hpp>
#include <boost/asio.hpp>
#include <condition_variable>
#include <json/json.h>
#include <map>
#include <mutex>
#include <pybind11/pybind11.h>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

namespace py = pybind11;

/**
 * @brief Manages signal values and provides serialization/deserialization capabilities.
 *
 * This class is responsible for storing signal values, providing access to them,
 * and handling serialization and deserialization of signal values to and from JSON.
 */
class SignalManager
{
public:
    SignalManager() = default;
    ~SignalManager() = default;

    /**
     * @brief Stops the signal manager and the associated service.
     */
    void stop();
    /**
     * @brief Sets the value of a signal.
     *
     * @param signal The name of the signal.
     * @param value The value to set, wrapped in a boost::any object.
     */
    void set_value( const std::string &signal, const boost::any &value );
    void set_value( const std::string &signal, const boost::any &value ) const;
    /**
     * @brief Gets the value of a signal.
     *
     * @param signal The name of the signal.
     * @return The value of the signal, wrapped in a boost::any object.
     */
    boost::any get_value( const std::string &signal ) const;

    void set_response_delay_ms_for_set( const std::string &signal, unsigned delayMs );
    void set_response_delay_ms_for_get( const std::string &signal, unsigned delayMs );

    /**
     * @brief Saves the current values of all signals to a JSON file.
     *
     * @param filename The name of the file to save the JSON data to.
     */
    void save_values( const std::string &filename ) const;
    /**
     * @brief Loads signal values from a JSON file.
     *
     * @param filename The name of the file to load the JSON data from.
     */
    void load_values( const std::string &filename );
    /**
     * @brief Retrieves a list of all signal names.
     *
     * @return A vector of strings containing the names of all signals.
     */
    std::vector<std::string> get_signals() const;

    std::string get_instance() const;

    /**
     * @brief Starts the signal manager and the associated service.
     *
     * @param domain The service domain.
     * @param instance The service instance.
     * @param connection The service connection.
     */
    void start( const std::string &domain, const std::string &instance, const std::string &connection );

private:
    std::mutex mInitMutex;
    std::condition_variable mInitCond;
    bool mIsInitialized = false;

    std::map<std::string, Signal> *mSignals = nullptr;
    void main( const std::string &domain, const std::string &instance, const std::string &connection );
    std::thread mThread;
    std::string mDomain;
    std::string mInstance;

    boost::asio::io_context mIoService;
};

// Standalone helper functions to serialize and deserialize boost::any using JsonCpp
Json::Value serialize_any( const boost::any &anyValue );
boost::any deserialize_any( const Json::Value &jsonValue, const std::type_info &expectedType );
