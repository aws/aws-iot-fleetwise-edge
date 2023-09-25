// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomDataSource.h"
#include "LoggingModule.h"
#include "Timer.h"
#include <unordered_map>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
const char *
CustomDataSource::getThreadName()
{
    // Maximum allowed: 15 characters
    return "CustomDataSource";
}

bool
CustomDataSource::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    // Make sure the thread goes into sleep immediately to wait for
    // the manifest to be available
    mShouldSleep.store( true );
    if ( !mThread.create( doWork, this ) )
    {
        FWE_LOG_TRACE( "Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Thread started" );
        const auto name = getThreadName();
        if ( name != nullptr )
        {
            mThread.setThreadName( name );
        }
    }

    return mThread.isActive() && mThread.isValid();
}

SignalID
CustomDataSource::getSignalIdFromStartBit( uint16_t startBit )
{
    std::lock_guard<std::mutex> lock( mExtractionOngoing );
    for ( auto &signal : mUsedMessageFormat.mSignals )
    {
        if ( signal.mFirstBitPosition == startBit )
        {
            return signal.mSignalID;
        }
    }
    return INVALID_SIGNAL_ID;
}

std::vector<CANSignalFormat>
CustomDataSource::getSignalInfo()
{
    std::lock_guard<std::mutex> lock( mExtractionOngoing );
    return mUsedMessageFormat.mSignals;
}

bool
CustomDataSource::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Thread stopped" );
    return !mThread.isActive();
}

bool
CustomDataSource::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
CustomDataSource::shouldSleep() const
{
    return mShouldSleep.load( std::memory_order_relaxed );
}

void
CustomDataSource::doWork( void *data )
{
    CustomDataSource *customDataSource = static_cast<CustomDataSource *>( data );
    Timer pollTimer;
    while ( !customDataSource->shouldStop() )
    {
        if ( customDataSource->shouldSleep() )
        {
            // We either just started or there was a decoder dictionary update that we can't use.
            // We should sleep
            FWE_LOG_TRACE( "No valid decoding information available so sleep" );
            // Wait here for the decoder dictionary to come.
            customDataSource->mWait.wait( Signal::WaitWithPredicate );
            // At this point, we should be able to see events coming as the channel is also
            // woken up.
        }
        if ( customDataSource->mNewMessageFormatExtracted )
        {
            std::lock_guard<std::mutex> lock( customDataSource->mExtractionOngoing );
            customDataSource->mUsedMessageFormat = customDataSource->mExtractedMessageFormat;
            customDataSource->mNewMessageFormatExtracted = false;
        }
        if ( ( !customDataSource->shouldSleep() ) && ( !customDataSource->mUsedMessageFormat.mSignals.empty() ) &&
             ( pollTimer.getElapsedMs().count() >= static_cast<int64_t>( customDataSource->mPollIntervalMs ) ) )
        {
            customDataSource->pollData();
            pollTimer.reset();
        }
        customDataSource->mWait.wait( customDataSource->mPollIntervalMs );
    }
}

void
CustomDataSource::setFilter( CANChannelNumericID canChannel, CANRawFrameID canRawFrameId )
{
    std::shared_ptr<const CANDecoderDictionary> canDecoderDictionary;
    {
        std::lock_guard<std::mutex> lock( mExtractionOngoing );
        mCanChannel = canChannel;
        mCanRawFrameId = canRawFrameId;
        canDecoderDictionary = mLastReceivedDictionary;
    }
    matchDictionaryToFilter( canDecoderDictionary, canChannel, canRawFrameId );
}

void
CustomDataSource::matchDictionaryToFilter( std::shared_ptr<const CANDecoderDictionary> &dictionary,
                                           CANChannelNumericID canChannel,
                                           CANRawFrameID canRawFrameId )
{
    if ( canChannel == INVALID_CAN_SOURCE_NUMERIC_ID )
    {
        FWE_LOG_TRACE( "CAN channel invalid, so requesting sleep" );
        mShouldSleep = true;
        return;
    }
    if ( dictionary == nullptr )
    {
        FWE_LOG_TRACE( "No decoder dictionary, so requesting sleep" );
        mShouldSleep = true;
        return;
    }
    for ( auto &channel : dictionary->canMessageDecoderMethod )
    {
        if ( channel.first == canChannel )
        {
            for ( auto &frame : channel.second )
            {
                if ( frame.first == canRawFrameId )
                {
                    {
                        std::lock_guard<std::mutex> lock( mExtractionOngoing );
                        mExtractedMessageFormat = frame.second.format;
                        mNewMessageFormatExtracted = true;
                        mShouldSleep = false;
                    }
                    FWE_LOG_TRACE( "Dictionary with relevant information for CustomDataSource so waking up" );
                    mWait.notify();
                    return;
                }
            }
            break;
        }
    }
    FWE_LOG_TRACE( "Dictionary has no relevant information for CustomDataSource" );
    mShouldSleep = true; // Nothing found
}

void
CustomDataSource::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                              VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol != VehicleDataSourceProtocol::RAW_SOCKET )
    {
        return;
    }
    CANChannelNumericID canChannel = 0;
    CANRawFrameID canRawFrameId = 0;
    auto canDecoderDictionary = std::dynamic_pointer_cast<const CANDecoderDictionary>( dictionary );
    {
        std::lock_guard<std::mutex> lock( mExtractionOngoing );
        mLastReceivedDictionary = canDecoderDictionary;
        canChannel = mCanChannel;
        canRawFrameId = mCanRawFrameId;
    }
    matchDictionaryToFilter( canDecoderDictionary, canChannel, canRawFrameId );
}

CustomDataSource::~CustomDataSource()
{
    if ( isRunning() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
