// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MySomeipDataSource.h"
#include <aws/iotfleetwise/LoggingModule.h>
#include <aws/iotfleetwise/SignalTypes.h>
#include <aws/iotfleetwise/Thread.h>
#include <chrono>
#include <utility>

MySomeipDataSource::MySomeipDataSource( std::shared_ptr<MySomeipInterfaceWrapper> mySomeipInterfaceWrapper,
                                        std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource,
                                        uint32_t cyclicUpdatePeriodMs )
    : mMySomeipInterfaceWrapper( std::move( mySomeipInterfaceWrapper ) )
    , mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mCyclicUpdatePeriodMs( cyclicUpdatePeriodMs )
{
}

MySomeipDataSource::~MySomeipDataSource()
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
    }
    // coverity[cert_err50_cpp_violation] false positive - join is called to exit the previous thread
    // coverity[autosar_cpp14_a15_5_2_violation] false positive - join is called to exit the previous thread
}

bool
MySomeipDataSource::connect()
{
    if ( !mMySomeipInterfaceWrapper->init() )
    {
        return false;
    }

    mProxy =
        std::dynamic_pointer_cast<v1::commonapi::MySomeipInterfaceProxy<>>( mMySomeipInterfaceWrapper->getProxy() );

    mXSubscription = mProxy->getXAttribute().getChangedEvent().subscribe( [this]( const int32_t &val ) {
        std::lock_guard<std::mutex> lock( mLastValMutex );
        mLastXVal = val;
        mLastXValAvailable = true;
        pushXValue( val );
    } );

    if ( mCyclicUpdatePeriodMs > 0 )
    {
        mThread = std::thread( [this]() {
            Aws::IoTFleetWise::Thread::setCurrentThreadName( "MySomeipDataSource" );
            while ( !mShouldStop )
            {
                // If the proxy is available, push the last vals periodically:
                {
                    std::lock_guard<std::mutex> lock( mLastValMutex );
                    if ( !mProxy->isAvailable() )
                    {
                        mLastXValAvailable = false;
                    }
                    else
                    {
                        if ( mLastXValAvailable )
                        {
                            pushXValue( mLastXVal );
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

void
MySomeipDataSource::pushXValue( const int32_t &val )
{
    mNamedSignalDataSource->ingestSignalValue(
        0,
        "Vehicle.MySomeipInterface.X",
        Aws::IoTFleetWise::DecodedSignalValue{ val, Aws::IoTFleetWise::SignalType::UINT32 } );
}
