// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#if defined( IOTFLEETWISE_LINUX )
// Includes
#include "CustomDataSource.h"
#include "Timer.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
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
        mLogger.trace( "CustomDataSource::start", "Thread failed to start" );
    }
    else
    {
        mLogger.trace( "CustomDataSource::start", "Thread started" );
        mThread.setThreadName( getThreadName() );
    }

    return mThread.isActive() && mThread.isValid();
}

SignalID
CustomDataSource::getSignalIdFromStartBit( uint16_t startBit )
{
    for ( auto &signal : mUsedMessageFormat.mSignals )
    {
        if ( signal.mFirstBitPosition == startBit )
        {
            return signal.mSignalID;
        }
    }
    return INVALID_SIGNAL_ID;
}

bool
CustomDataSource::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "CustomDataSource::stop", "Thread stopped" );
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
            customDataSource->mLogger.trace( "CustomDataSource::doWork",
                                             "No valid decoding information available so sleep" );
            // Wait here for the decoder dictionary to come.
            customDataSource->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
            // At this point, we should be able to see events coming as the channel is also
            // woken up.
        }
        if ( customDataSource->mNewMessageFormatExtracted )
        {
            std::lock_guard<std::mutex> lock( customDataSource->mExtractionOngoing );
            customDataSource->mUsedMessageFormat = customDataSource->mExtractedMessageFormat;
            customDataSource->mNewMessageFormatExtracted = false;
        }
        if ( !customDataSource->shouldSleep() && !customDataSource->mUsedMessageFormat.mSignals.empty() &&
             pollTimer.getElapsedMs().count() >= static_cast<int64_t>( customDataSource->mPollIntervalMs ) )
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
        canDecoderDictionary = lastReceivedDictionary;
    }
    if ( canDecoderDictionary != nullptr )
    {
        matchDictionaryToFilter( *canDecoderDictionary, canChannel, canRawFrameId );
    }
}

void
CustomDataSource::matchDictionaryToFilter( const CANDecoderDictionary &dictionary,
                                           CANChannelNumericID canChannel,
                                           CANRawFrameID canRawFrameId )
{
    if ( mCanChannel == INVALID_CAN_SOURCE_NUMERIC_ID )
    {
        mLogger.trace( "CustomDataSource::matchDictionaryToFilter", "No Valid CAN so requesting sleep" );
        mShouldSleep = true; // Nothing found
        return;
    }
    for ( auto &channel : dictionary.canMessageDecoderMethod )
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
                    mLogger.trace( "CustomDataSource::matchDictionaryToFilter",
                                   "Dictionary with relevant information for CustomDataSource so waking up" );
                    mWait.notify();
                    return;
                }
            }
            break;
        }
    }
    mLogger.trace( "CustomDataSource::matchDictionaryToFilter",
                   "Dictionary has no relevant information for CustomDataSource" );
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
        lastReceivedDictionary = canDecoderDictionary;
        canChannel = mCanChannel;
        canRawFrameId = mCanRawFrameId;
    }
    if ( canDecoderDictionary != nullptr )
    {
        matchDictionaryToFilter( *canDecoderDictionary, canChannel, canRawFrameId );
    }
}
CustomDataSource::~CustomDataSource()
{
    if ( isRunning() )
    {
        stop();
    }
}
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
#endif
