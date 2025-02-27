// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MyCounterDataSource.h"
#include <aws/iotfleetwise/LoggingModule.h>
#include <aws/iotfleetwise/SignalTypes.h>
#include <aws/iotfleetwise/Thread.h>
#include <chrono>
#include <string>

MyCounterDataSource::MyCounterDataSource(
    std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource,
    uint32_t configOption1,
    Aws::IoTFleetWise::RawData::BufferManager *rawDataBufferManager )
    : mNamedSignalDataSource( namedSignalDataSource )
    , mConfigOption1( configOption1 )
    , mRawDataBufferManager( rawDataBufferManager )
{
    mThread = std::thread( [&]() {
        Aws::IoTFleetWise::Thread::setCurrentThreadName( "MyCounterIfc" );

        while ( !mThreadShouldStop )
        {
            mCounter += mConfigOption1;
            std::string stringValue = "Hello world! " + std::to_string( mCounter );

            // Example of string value ingestion
            auto signalId = mNamedSignalDataSource->getNamedSignalID( SIGNAL_NAME );
            if ( signalId != Aws::IoTFleetWise::INVALID_SIGNAL_ID )
            {
                auto receiveTime = mClock->systemTimeSinceEpochMs();
                auto bufferHandle =
                    mRawDataBufferManager->push( reinterpret_cast<const uint8_t *>( stringValue.data() ),
                                                 stringValue.size(),
                                                 receiveTime,
                                                 signalId );
                if ( bufferHandle == Aws::IoTFleetWise::RawData::INVALID_BUFFER_HANDLE )
                {
                    FWE_LOG_WARN( "Raw message id: " + std::to_string( signalId ) +
                                  "  was rejected by RawBufferManager" );
                }
                else
                {
                    // immediately set usage hint so buffer handle does not get directly deleted again
                    mRawDataBufferManager->increaseHandleUsageHint(
                        signalId,
                        bufferHandle,
                        Aws::IoTFleetWise::RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );
                    mNamedSignalDataSource->ingestSignalValue(
                        receiveTime,
                        SIGNAL_NAME,
                        Aws::IoTFleetWise::DecodedSignalValue{ bufferHandle, Aws::IoTFleetWise::SignalType::STRING } );
                }
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        }
    } );
}

MyCounterDataSource::~MyCounterDataSource()
{
    mThreadShouldStop = true;
    mThread.join();
}
