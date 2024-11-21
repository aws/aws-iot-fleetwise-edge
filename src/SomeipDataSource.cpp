// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SomeipDataSource.h"
#include "LoggingModule.h"
#include "SignalTypes.h"
#include "Thread.h"
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
                                    std::shared_ptr<RawData::BufferManager> rawDataBufferManager,
                                    uint32_t cyclicUpdatePeriodMs )
    : mExampleSomeipInterfaceWrapper( std::move( exampleSomeipInterfaceWrapper ) )
    , mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mRawBufferManager( std::move( rawDataBufferManager ) )
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
SomeipDataSource::pushA1Value( const v1::commonapi::CommonTypes::a1Struct &val )
{
    std::vector<std::pair<std::string, DecodedSignalValue>> values;
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.A",
                                         DecodedSignalValue{ val.getA2().getA(), SignalType::INT32 } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.B",
                                         DecodedSignalValue{ val.getA2().getB(), SignalType::BOOLEAN } ) );
    values.emplace_back( std::make_pair( "Vehicle.ExampleSomeipInterface.A1.A2.D",
                                         DecodedSignalValue{ val.getA2().getD(), SignalType::DOUBLE } ) );
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

    if ( mRawBufferManager == nullptr )
    {
        FWE_LOG_WARN( "Raw message id: " + std::to_string( signalID ) + " can not be handed over to RawBufferManager" );
        return;
    }
    auto receiveTime = mClock->systemTimeSinceEpochMs();
    std::vector<uint8_t> buffer( stringValue.begin(), stringValue.end() );
    auto bufferHandle = mRawBufferManager->push( ( buffer.data() ), buffer.size(), receiveTime, signalID );

    if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
    {
        FWE_LOG_WARN( "Raw message id: " + std::to_string( signalID ) + "  was rejected by RawBufferManager" );
        return;
    }
    // immediately set usage hint so buffer handle does not get directly deleted again
    mRawBufferManager->increaseHandleUsageHint(
        signalID, bufferHandle, RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );

    mNamedSignalDataSource->ingestSignalValue(
        receiveTime, signalName, DecodedSignalValue{ bufferHandle, SignalType::STRING } );
}

bool
SomeipDataSource::init()
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
                {
                    std::lock_guard<std::mutex> lock( mLastValMutex );
                    if ( !mProxy->isAvailable() )
                    {
                        mLastXValAvailable = false;
                        mLastA1ValAvailable = false;
                    }
                    else
                    {
                        if ( mLastXValAvailable )
                        {
                            pushXValue( mLastXVal );
                        }
                        if ( mLastA1ValAvailable )
                        {
                            pushA1Value( mLastA1Val );
                        }
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
