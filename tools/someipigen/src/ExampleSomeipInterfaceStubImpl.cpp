// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExampleSomeipInterfaceStubImpl.hpp"
#include "v1/commonapi/CommonTypes.hpp"
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

    mSignals["Vehicle.ExampleSomeipInterface.Temperature"] = Signal( boost::any( static_cast<int32_t>( 1 ) ), [this]() {
        fireTemperatureAttributeChanged(
            boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.Temperature"].value ) );
    } );

    auto onA1AttributeChanged = [this]() {
        v1::commonapi::CommonTypes::a1Struct a1;
        a1.setS( boost::any_cast<std::string>( mSignals["Vehicle.ExampleSomeipInterface.A1.S"].value ) );
        a1.setA( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.A"].value ) );
        a1.setB( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.B"].value ) );
        a1.setC( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.C"].value ) );
        a1.setD( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.D"].value ) );
        a1.setE( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.E"].value ) );
        a1.setF( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.F"].value ) );
        a1.setG( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.G"].value ) );
        a1.setH( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.H"].value ) );
        a1.setI( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.I"].value ) );
        a1.setJ( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.J"].value ) );
        a1.setK( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.K"].value ) );
        a1.setL( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.L"].value ) );
        v1::commonapi::CommonTypes::a2Struct a2;
        a2.setA( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.A"].value ) );
        a2.setB( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.B"].value ) );
        a2.setD( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.D"].value ) );
        a2.setE( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.E"].value ) );
        a2.setF( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.F"].value ) );
        a2.setG( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.G"].value ) );
        a2.setH( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.H"].value ) );
        a2.setI( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.I"].value ) );
        a2.setJ( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.J"].value ) );
        a2.setK( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.K"].value ) );
        a2.setL( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.L"].value ) );
        a2.setM( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.M"].value ) );
        a2.setN( boost::any_cast<int32_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.N"].value ) );
        a2.setO( boost::any_cast<bool>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.O"].value ) );
        a2.setP( boost::any_cast<double>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.P"].value ) );
        a2.setQ( boost::any_cast<int64_t>( mSignals["Vehicle.ExampleSomeipInterface.A1.A2.Q"].value ) );
        a1.setA2( a2 );
        fireA1AttributeChanged( a1 );
    };
    mSignals["Vehicle.ExampleSomeipInterface.A1.S"] = Signal( boost::any( std::string( "hi" ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.B"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.C"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.D"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.E"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.F"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.G"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.H"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.I"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.J"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.K"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.L"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.A"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.B"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.D"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.E"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.F"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.G"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.H"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.I"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.J"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.K"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.L"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.M"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.N"] =
        Signal( boost::any( static_cast<int32_t>( 100 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.O"] = Signal( boost::any( false ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.P"] =
        Signal( boost::any( static_cast<double>( 1.0 ) ), onA1AttributeChanged );
    mSignals["Vehicle.ExampleSomeipInterface.A1.A2.Q"] =
        Signal( boost::any( static_cast<int64_t>( 100 ) ), onA1AttributeChanged );

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
