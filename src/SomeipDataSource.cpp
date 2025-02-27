// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/SomeipDataSource.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

SomeipDataSource::SomeipDataSource( std::shared_ptr<ExampleSomeipInterfaceWrapper> exampleSomeipInterfaceWrapper,
                                    std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                    RawData::BufferManager *rawDataBufferManager,
                                    uint32_t cyclicUpdatePeriodMs )
    : mExampleSomeipInterfaceWrapper( std::move( exampleSomeipInterfaceWrapper ) )
    , mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mRawDataBufferManager( rawDataBufferManager )
    , mCyclicUpdatePeriodMs( cyclicUpdatePeriodMs )
{
}

SomeipDataSource::~SomeipDataSource()
{
    if ( mThread.joinable() )
    {
        mShouldStop = true;
        mThread.join();
    }
    if ( mProxy )
    {
        if ( mXSubscription != 0 )
        {
            mProxy->getXAttribute().getChangedEvent().unsubscribe( mXSubscription );
        }
        if ( mTemperatureSubscription != 0 )
        {
            mProxy->getTemperatureAttribute().getChangedEvent().unsubscribe( mTemperatureSubscription );
        }
        if ( mA1Subscription != 0 )
        {
            mProxy->getA1Attribute().getChangedEvent().unsubscribe( mA1Subscription );
        }
    }
    // coverity[cert_err50_cpp_violation] false positive - join is called to exit the previous thread
    // coverity[autosar_cpp14_a15_5_2_violation] false positive - join is called to exit the previous thread
}

void
SomeipDataSource::pushXValue( const int32_t &val )
{
    mNamedSignalDataSource->ingestSignalValue(
        0, "Vehicle.ExampleSomeipInterface.X", DecodedSignalValue{ val, SignalType::UINT32 } );
}

void
SomeipDataSource::pushTemperatureValue( const int32_t &val )
{
    mNamedSignalDataSource->ingestSignalValue(
        0, "Vehicle.ExampleSomeipInterface.Temperature", DecodedSignalValue{ val, SignalType::INT32 } );
}

void
SomeipDataSource::pushA1Value( const v1::commonapi::CommonTypes::a1Struct &val )
{
    std::vector<std::pair<std::string, DecodedSignalValue>> values;
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A",
                                         DecodedSignalValue{ val.getA(), SignalType::BOOLEAN } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.B", DecodedSignalValue{ val.getB(), SignalType::INT32 } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.C", DecodedSignalValue{ val.getC(), SignalType::DOUBLE } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.D", DecodedSignalValue{ val.getD(), SignalType::INT64 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.E",
                                         DecodedSignalValue{ val.getE(), SignalType::BOOLEAN } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.F", DecodedSignalValue{ val.getF(), SignalType::INT32 } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.G", DecodedSignalValue{ val.getG(), SignalType::DOUBLE } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.H", DecodedSignalValue{ val.getH(), SignalType::INT64 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.I",
                                         DecodedSignalValue{ val.getI(), SignalType::BOOLEAN } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.J", DecodedSignalValue{ val.getJ(), SignalType::INT32 } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.K", DecodedSignalValue{ val.getK(), SignalType::DOUBLE } ) );
    values.emplace_back(
        std::make_pair( "Vehicle.ExampleSomeipInterface.A1.L", DecodedSignalValue{ val.getL(), SignalType::INT64 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.A",
                                         DecodedSignalValue{ val.getA2().getA(), SignalType::INT32 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.B",
                                         DecodedSignalValue{ val.getA2().getB(), SignalType::BOOLEAN } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.D",
                                         DecodedSignalValue{ val.getA2().getD(), SignalType::DOUBLE } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.E",
                                         DecodedSignalValue{ val.getA2().getE(), SignalType::INT64 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.F",
                                         DecodedSignalValue{ val.getA2().getF(), SignalType::INT32 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.G",
                                         DecodedSignalValue{ val.getA2().getG(), SignalType::BOOLEAN } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.H",
                                         DecodedSignalValue{ val.getA2().getH(), SignalType::DOUBLE } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.I",
                                         DecodedSignalValue{ val.getA2().getI(), SignalType::INT64 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.J",
                                         DecodedSignalValue{ val.getA2().getJ(), SignalType::INT32 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.K",
                                         DecodedSignalValue{ val.getA2().getK(), SignalType::BOOLEAN } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.L",
                                         DecodedSignalValue{ val.getA2().getL(), SignalType::DOUBLE } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.M",
                                         DecodedSignalValue{ val.getA2().getM(), SignalType::INT64 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.N",
                                         DecodedSignalValue{ val.getA2().getN(), SignalType::INT32 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.O",
                                         DecodedSignalValue{ val.getA2().getO(), SignalType::BOOLEAN } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.P",
                                         DecodedSignalValue{ val.getA2().getP(), SignalType::DOUBLE } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.Q",
                                         DecodedSignalValue{ val.getA2().getQ(), SignalType::INT64 } ) );
    pushStringSignalToNamedDataSource( "Vehicle.ExampleSomeipInterface.A1.S", val.getS() );
    mNamedSignalDataSource->ingestMultipleSignalValues( 0, values );
}

void
SomeipDataSource::pushStringSignalToNamedDataSource( const std::string &signalName, const std::string &stringValue )
{
    SignalID signalID = mNamedSignalDataSource->getNamedSignalID( signalName );
    if ( signalID == INVALID_SIGNAL_ID )
    {
        FWE_LOG_TRACE( "No decoding rules set for signal name " + signalName );
        return;
    }

    if ( mRawDataBufferManager == nullptr )
    {
        FWE_LOG_WARN( "Raw message id: " + std::to_string( signalID ) + " can not be handed over to RawBufferManager" );
        return;
    }
    auto receiveTime = mClock->systemTimeSinceEpochMs();
    std::vector<uint8_t> buffer( stringValue.begin(), stringValue.end() );
    auto bufferHandle = mRawDataBufferManager->push( ( buffer.data() ), buffer.size(), receiveTime, signalID );

    if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
    {
        FWE_LOG_WARN( "Raw message id: " + std::to_string( signalID ) + "  was rejected by RawBufferManager" );
        return;
    }
    // immediately set usage hint so buffer handle does not get directly deleted again
    mRawDataBufferManager->increaseHandleUsageHint(
        signalID, bufferHandle, RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );

    mNamedSignalDataSource->ingestSignalValue(
        receiveTime, signalName, DecodedSignalValue{ bufferHandle, SignalType::STRING } );
}

bool
SomeipDataSource::connect()
{
    if ( !mExampleSomeipInterfaceWrapper->init() )
    {
        return false;
    }

    mProxy = std::dynamic_pointer_cast<v1::commonapi::ExampleSomeipInterfaceProxy<>>(
        mExampleSomeipInterfaceWrapper->getProxy() );

    mXSubscription = mProxy->getXAttribute().getChangedEvent().subscribe( [this]( const int32_t &val ) {
        std::lock_guard<std::mutex> lock( mLastValMutex );
        mLastXVal = val;
        mLastXValAvailable = true;
        pushXValue( val );
    } );

    mTemperatureSubscription =
        mProxy->getTemperatureAttribute().getChangedEvent().subscribe( [this]( const int32_t &val ) {
            std::lock_guard<std::mutex> lock( mLastValMutex );
            mLastTemperatureVal = val;
            mLastTemperatureValAvailable = true;
            pushTemperatureValue( val );
        } );

    mA1Subscription = mProxy->getA1Attribute().getChangedEvent().subscribe(
        [this]( const v1::commonapi::CommonTypes::a1Struct &val ) {
            std::lock_guard<std::mutex> lock( mLastValMutex );
            mLastA1Val = val;
            mLastA1ValAvailable = true;
            pushA1Value( val );
        } );

    if ( mCyclicUpdatePeriodMs > 0 )
    {
        mThread = std::thread( [this]() {
            Thread::setCurrentThreadName( "SomeipDataSource" );
            while ( !mShouldStop )
            {
                // If the proxy is available, push the last vals periodically:
                if ( !mProxy->isAvailable() )
                {
                    std::lock_guard<std::mutex> lock( mLastValMutex );
                    mLastXValAvailable = false;
                    mLastTemperatureValAvailable = false;
                    mLastA1ValAvailable = false;
                }
                else
                {
                    std::lock_guard<std::mutex> lock( mLastValMutex );
                    if ( mLastXValAvailable )
                    {
                        pushXValue( mLastXVal );
                    }

                    if ( mLastTemperatureValAvailable )
                    {
                        pushTemperatureValue( mLastTemperatureVal );
                    }

                    if ( mLastA1ValAvailable )
                    {
                        pushA1Value( mLastA1Val );
                    }
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( mCyclicUpdatePeriodMs ) );
            }
        } );
    }

    FWE_LOG_INFO( "Successfully initialized" );
    return true;
}

} // namespace IoTFleetWise
} // namespace Aws
