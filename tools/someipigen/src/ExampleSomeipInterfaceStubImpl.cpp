// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExampleSomeipInterfaceStubImpl.hpp"
#include <boost/type_index/type_index_facade.hpp>

ExampleSomeipInterfaceStubImpl::ExampleSomeipInterfaceStubImpl( boost::asio::io_context &io_context )
    : mIoContext( io_context )
{
    mSignals["Int32"] = Signal( boost::any( static_cast<int32_t>( 0 ) ) );
    mSignals["Int32LRC"] = Signal( boost::any( static_cast<int32_t>( 0 ) ) );
    mSignals["Int64"] = Signal( boost::any( static_cast<int64_t>( 0 ) ) );
    mSignals["Boolean"] = Signal( boost::any( static_cast<bool>( 0 ) ) );
    mSignals["Float"] = Signal( boost::any( static_cast<float>( 0 ) ) );
    mSignals["Double"] = Signal( boost::any( static_cast<double>( 0 ) ) );
    mSignals["String"] = Signal( boost::any( std::string() ) );

    mSignals["Vehicle.ExampleSomeipInterface.X"] = Signal( boost::any( static_cast<int32_t>( 1 ) ), [this]() {
        fireXAttributeChanged( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.X"].value ) );
    } );

    auto onA1AttributeChanged = [this]() {
        v1::commonapi::CommonTypes::a1Struct a1;
        a1.setS( boost::any_cast<std::string>( mSignals["Vehicle.ExampleSomeipInterface.A1.S"].value ) );
        v1::commonapi::CommonTypes::a2Struct a2;
        a2.setA( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.A"].value ) );
        a2.setB( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.B"].value ) );
        a2.setD( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.D"].value ) );
        a1.setA2( a2 );
        fireA1AttributeChanged( a1 );
    };
    mSignals["Vehicle.ExampleSomeipInterface.A1.S"] = Signal( boost::any( std::string( "hi" ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.A"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.B"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.D"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );

    // Now that all initial values have been set, call of the onChange methods to set the initial values:
    for ( auto &signal : mSignals )
    {
        if ( signal.second.onChange )
        {
            signal.second.onChange();
        }
    }
}

void
ExampleSomeipInterfaceStubImpl::setInt32( const std::shared_ptr<CommonAPI::ClientId> client,
                                          int32_t value,
                                          setInt32Reply_t reply )
{
    static_cast<void>( client );
    setValueAsync( "Int32", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::setInt32LongRunning( const std::shared_ptr<CommonAPI::ClientId> client,
                                                     std::string commandId,
                                                     int32_t value,
                                                     setInt32LongRunningReply_t reply )
{
    static_cast<void>( client );
    setValueLRCAsync( commandId, "Int32LRC", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::getInt32LongRunning( const std::shared_ptr<CommonAPI::ClientId> client,
                                                     getInt32Reply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "Int32LRC", reply );
}

void
ExampleSomeipInterfaceStubImpl::getInt32( const std::shared_ptr<CommonAPI::ClientId> client, getInt32Reply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "Int32", reply );
}

void
ExampleSomeipInterfaceStubImpl::setInt64( const std::shared_ptr<CommonAPI::ClientId> client,
                                          int64_t value,
                                          setInt64Reply_t reply )
{
    static_cast<void>( client );
    setValueAsync( "Int64", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::getInt64( const std::shared_ptr<CommonAPI::ClientId> client, getInt64Reply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "Int64", reply );
}

void
ExampleSomeipInterfaceStubImpl::setBoolean( const std::shared_ptr<CommonAPI::ClientId> client,
                                            bool value,
                                            setBooleanReply_t reply )
{
    static_cast<void>( client );
    setValueAsync( "Boolean", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::getBoolean( const std::shared_ptr<CommonAPI::ClientId> client, getBooleanReply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "Boolean", reply );
}

void
ExampleSomeipInterfaceStubImpl::setFloat( const std::shared_ptr<CommonAPI::ClientId> client,
                                          float value,
                                          setFloatReply_t reply )
{
    static_cast<void>( client );
    setValueAsync( "Float", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::getFloat( const std::shared_ptr<CommonAPI::ClientId> client, getFloatReply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "Float", reply );
}

void
ExampleSomeipInterfaceStubImpl::setDouble( const std::shared_ptr<CommonAPI::ClientId> client,
                                           double value,
                                           setDoubleReply_t reply )
{
    static_cast<void>( client );
    setValueAsync( "Double", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::getDouble( const std::shared_ptr<CommonAPI::ClientId> client, getDoubleReply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "Double", reply );
}

void
ExampleSomeipInterfaceStubImpl::setString( const std::shared_ptr<CommonAPI::ClientId> client,
                                           std::string value,
                                           setStringReply_t reply )
{
    static_cast<void>( client );
    setValueAsync( "String", value, reply );
}

void
ExampleSomeipInterfaceStubImpl::getString( const std::shared_ptr<CommonAPI::ClientId> client, getStringReply_t reply )
{
    static_cast<void>( client );
    getValueAsync( "String", reply );
}
