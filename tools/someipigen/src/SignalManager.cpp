// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SignalManager.hpp"
#include "v1/commonapi/ExampleSomeipInterface.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <boost/type_index/type_index_facade.hpp>
#include <cstdint> // IWYU pragma: keep
#include <fstream> // IWYU pragma: keep
#include <functional>
#include <iterator>
#include <json/json.h>
#include <memory>
#include <stdexcept>
#include <utility>

// Find the signal in the map and set its value if it exists
void
SignalManager::set_value( const std::string &signal, const boost::any &value )
{
    if ( !mSignals )
    {
        throw std::runtime_error( "Signals map is not initialized." );
    }
    auto it = mSignals->find( signal );
    if ( it == mSignals->end() )
    {
        throw std::runtime_error( "Signal not found: " + signal );
    }
    it->second.value = value;
    if ( it->second.onChange )
    {
        it->second.onChange();
    }
}

boost::any
SignalManager::get_value( const std::string &signal ) const
{
    if ( !mSignals )
    {
        throw std::runtime_error( "Signals map is not initialized." );
    }
    auto it = mSignals->find( signal );
    if ( it == mSignals->end() )
    {
        throw std::runtime_error( "Signal not found: " + signal );
    }
    return it->second.value;
}

void
SignalManager::set_response_delay_ms_for_set( const std::string &signal, unsigned delayMs )
{
    if ( !mSignals )
    {
        throw std::runtime_error( "Signals map is not initialized." );
    }

    auto it = mSignals->find( signal );
    if ( it == mSignals->end() )
    {
        throw std::runtime_error( "Signal not found: " + signal );
    }

    it->second.responseDelayMsForSetMethod = delayMs;
}

void
SignalManager::set_response_delay_ms_for_get( const std::string &signal, unsigned delayMs )
{
    if ( !mSignals )
    {
        throw std::runtime_error( "Signals map is not initialized." );
    }

    auto it = mSignals->find( signal );
    if ( it == mSignals->end() )
    {
        throw std::runtime_error( "Signal not found: " + signal );
    }

    it->second.responseDelayMsForGetMethod = delayMs;
}

void
SignalManager::save_values( const std::string &filename ) const
{
    // Serialize the current values of all signals to a JSON file
    Json::Value root;
    for ( const auto &pair : *mSignals )
    {
        root[pair.first] = serialize_any( pair.second.value );
    }
    std::ofstream file( filename );
    if ( file.fail() )
    {
        throw std::runtime_error( "Failed to open " + filename );
    }
    file << root;
    if ( file.fail() )
    {
        throw std::runtime_error( "Failed to write to: " + filename );
    }
}

void
SignalManager::load_values( const std::string &filename )
{
    std::ifstream file( filename, std::ios::in );
    if ( file.fail() )
    {
        throw std::runtime_error( "Failed to open " + filename );
    }
    std::string contents( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    if ( file.fail() )
    {
        throw std::runtime_error( "Failed to read from: " + filename );
    }
    if ( contents.empty() )
    {
        throw std::runtime_error( "File is empty: " + filename );
    }
    Json::Reader reader;
    Json::Value root;
    if ( !reader.parse( contents, root, false ) )
    {
        throw std::runtime_error( "Failed to parse JSON content: " + reader.getFormattedErrorMessages() );
    }
    for ( const auto &key : root.getMemberNames() )
    {
        auto it = mSignals->find( key );
        if ( it != mSignals->end() )
        {
            try
            {
                it->second.value = deserialize_any( root[key], it->second.value.type() );
            }
            catch ( const std::runtime_error &e )
            {
                throw std::runtime_error( "Error deserializing value for signal: " + key + " - " + e.what() );
            }
        }
    }
}

std::vector<std::string>
SignalManager::get_signals() const
{
    std::vector<std::string> signalNames;
    for ( const auto &pair : *mSignals )
    {
        signalNames.push_back( pair.first );
    }
    return signalNames;
}

std::string
SignalManager::get_instance() const
{
    return mInstance;
}

void
SignalManager::main( const std::string &domain, const std::string &instance, const std::string &connection )
{
    mDomain = domain;
    mInstance = instance;
    // Set up the service and register it with the runtime
    CommonAPI::Runtime::setProperty( "LogContext", "Someipigen" );
    CommonAPI::Runtime::setProperty( "LogApplication", "Someipigen" );
    CommonAPI::Runtime::setProperty( "LibraryBase", "SomeipigenAttributes" );

    std::shared_ptr<ExampleSomeipInterfaceStubImpl> someipigenService =
        std::make_shared<ExampleSomeipInterfaceStubImpl>( mIoService );
    if ( !CommonAPI::Runtime::get()->registerService( mDomain, mInstance, someipigenService, connection ) )
    {
        throw std::runtime_error( "Failed to register service" );
    }

    // Store a reference to the signals map for later use
    mSignals = &( someipigenService->getSignals() );

    // We are blocking `SignalManager::star` until the everything gets initialised
    {
        std::lock_guard<std::mutex> lock( mInitMutex );
        mIsInitialized = true;
    }
    mInitCond.notify_one();

    // Prevent the io context loop from exiting when it runs out of work
    auto workGuard = boost::asio::make_work_guard( mIoService );
    mIoService.run();
}

void
SignalManager::stop()
{
    mIoService.stop();
    if ( mThread.joinable() )
    {
        mThread.join();
    }
    if ( !CommonAPI::Runtime::get()->unregisterService(
             mDomain, v1::commonapi::ExampleSomeipInterface::getInterface(), mInstance ) )
    {
        throw std::runtime_error( "Failed to unregister service" );
    }
}

void
SignalManager::start( const std::string &domain, const std::string &instance, const std::string &connection )
{
    mThread = std::thread( &SignalManager::main, this, domain, instance, connection );
    // wait until someipigenService is registered
    std::unique_lock<std::mutex> lock( mInitMutex );
    mInitCond.wait( lock, [this] {
        return mIsInitialized;
    } );
}

Json::Value
serialize_any( const boost::any &anyValue )
{
    Json::Value value;

    if ( anyValue.type() == typeid( int ) )
    {
        value = Json::Value( boost::any_cast<int>( anyValue ) );
    }
    else if ( anyValue.type() == typeid( int32_t ) )
    {
        value = Json::Value( static_cast<Json::Int>( boost::any_cast<int32_t>( anyValue ) ) );
    }
    else if ( anyValue.type() == typeid( uint32_t ) )
    {
        value = Json::Value( static_cast<Json::UInt>( boost::any_cast<uint32_t>( anyValue ) ) );
    }
    else if ( anyValue.type() == typeid( int64_t ) )
    {
        value = Json::Value( static_cast<Json::Int>( boost::any_cast<int64_t>( anyValue ) ) );
    }
    else if ( anyValue.type() == typeid( uint64_t ) )
    {
        value = Json::Value( static_cast<Json::UInt>( boost::any_cast<uint64_t>( anyValue ) ) );
    }
    else if ( anyValue.type() == typeid( float ) )
    {
        value = Json::Value( boost::any_cast<float>( anyValue ) );
    }
    else if ( anyValue.type() == typeid( double ) )
    {
        value = Json::Value( boost::any_cast<double>( anyValue ) );
    }
    else if ( anyValue.type() == typeid( std::string ) )
    {
        value = Json::Value( boost::any_cast<std::string>( anyValue ) );
    }
    else if ( anyValue.type() == typeid( bool ) )
    {
        value = Json::Value( boost::any_cast<bool>( anyValue ) );
    }
    else
    {
        throw std::runtime_error( "Unsupported boost::any type for serialization" );
    }
    return value;
}

boost::any
deserialize_any( const Json::Value &jsonValue, const std::type_info &expectedType )
{

    if ( expectedType == typeid( int ) && jsonValue.isInt() )
    {
        return boost::any( jsonValue.asInt() );
    }
    else if ( expectedType == typeid( int32_t ) && jsonValue.isInt() )
    {
        return boost::any( static_cast<int32_t>( jsonValue.asInt() ) );
    }
    else if ( expectedType == typeid( uint32_t ) && jsonValue.isUInt() )
    {
        return boost::any( static_cast<uint32_t>( jsonValue.asUInt() ) );
    }
    else if ( expectedType == typeid( int64_t ) && jsonValue.isInt64() )
    {
        return boost::any( static_cast<int64_t>( jsonValue.asInt64() ) );
    }
    else if ( expectedType == typeid( uint64_t ) && jsonValue.isUInt64() )
    {
        return boost::any( static_cast<uint64_t>( jsonValue.asUInt64() ) );
    }
    else if ( expectedType == typeid( float ) && jsonValue.isDouble() )
    {
        return boost::any( jsonValue.asFloat() );
    }
    else if ( expectedType == typeid( double ) && jsonValue.isDouble() )
    {
        return boost::any( jsonValue.asDouble() );
    }
    else if ( expectedType == typeid( std::string ) && jsonValue.isString() )
    {
        return boost::any( jsonValue.asString() );
    }
    else if ( expectedType == typeid( bool ) && jsonValue.isBool() )
    {
        return boost::any( jsonValue.asBool() );
    }
    else
    {
        throw std::runtime_error( "JSON type does not match expected boost::any type" );
    }
}
