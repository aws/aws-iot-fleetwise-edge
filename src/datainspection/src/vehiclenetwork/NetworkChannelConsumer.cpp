/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

// Includes
#include "NetworkChannelConsumer.h"
#include "TraceModule.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <sstream>
#include <string.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
NetworkChannelConsumer::NetworkChannelConsumer()
{
    mIdleTime = DEFAULT_THREAD_IDLE_TIME_MS;
}

NetworkChannelConsumer::~NetworkChannelConsumer()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
NetworkChannelConsumer::init( uint8_t canChannelID,
                              SignalBufferPtr signalBufferPtr,
                              CANBufferPtr canBufferPtr,
                              uint32_t idleTimeMs )
{
    mID = generateChannelConsumerID();
    mCANDecoder.reset( new CANDecoder() );
    if ( signalBufferPtr.get() == nullptr || canBufferPtr.get() == nullptr )
    {
        mLogger.trace( "NetworkChannelConsumer::init", " Init Failed due to bufferPtr as nullptr " );
        return false;
    }
    else
    {
        mLogger.trace( "NetworkChannelConsumer::init",
                       "Init Network channel consumer with id: " + std::to_string( canChannelID ) );
        mCANChannelID = canChannelID;
        mSignalBufferPtr = signalBufferPtr;
        mCANBufferPtr = canBufferPtr;
    }
    if ( idleTimeMs != 0 )
    {
        mIdleTime = idleTimeMs;
    }
    return true;
}

void
NetworkChannelConsumer::suspendDataConsumption()
{
    // Go back to sleep
    mLogger.trace( "NetworkChannelConsumer::suspendDataConsumption",
                   "Going to sleep until a the resume signal. Consumer : " + std::to_string( mCANChannelID ) );
    mShouldSleep.store( true, std::memory_order_relaxed );
}

void
NetworkChannelConsumer::resumeDataConsumption( ConstDecoderDictionaryConstPtr &dictionary )
{
    if ( dictionary.get() != nullptr )
    {
        {
            // As this function can be asynchronously invoked by other module in the middle of
            // CAN message Decoding, a mutex has to be used to avoid decoding race condition in which
            // a single CAN message could got decoded by two different formula. This mutex ensure
            // the shared pointer assignment is atomic
            std::lock_guard<std::mutex> lock( mDecoderDictMutex );
            // Convert the Generic Decoder Dictionary to CAN Decoder Dictionary
            mDecoderDictionaryConstPtr = dictionary;
        }

        const auto &decoderMethod = dictionary->canMessageDecoderMethod;
        std::string canIds = "";
        // check if this CAN message ID on this CAN Channel has the decoder method and if yes log the number
        if ( decoderMethod.find( mCANChannelID ) != decoderMethod.cend() )
        {
            for ( auto &decode : decoderMethod.at( mCANChannelID ) )
            {
                canIds += std::to_string( decode.first ) + ", ";
            }
        }
        mLogger.trace( "NetworkChannelConsumer::resumeDataConsumption",
                       " Changing Decoder Dictionary on Consumer :" + std::to_string( mID ) +
                           " with decoding rules for CAN-IDs: " + canIds );
        // Make sure the thread does not sleep anymore
        mShouldSleep.store( false );
        // Wake up the worker thread.
        mWait.notify();
    }
}

bool
NetworkChannelConsumer::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    // Make sure the thread goes into sleep immediately to wait for
    // the manifest to be available
    mShouldSleep.store( true );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.trace( "NetworkChannelConsumer::start", " Consumer Thread failed to start " );
    }
    else
    {
        mLogger.trace( "NetworkChannelConsumer::start", " Consumer Thread started " );
        mThread.setThreadName( "fwDIConsumer" + std::to_string( mID ) );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
NetworkChannelConsumer::stop()
{
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    mLogger.trace( "NetworkChannelConsumer::stop", " Consumer Thread stopped " );
    return !mThread.isActive();
}

bool
NetworkChannelConsumer::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
NetworkChannelConsumer::shouldSleep() const
{
    return mShouldSleep.load( std::memory_order_relaxed );
}

void
NetworkChannelConsumer::doWork( void *data )
{

    NetworkChannelConsumer *consumer = static_cast<NetworkChannelConsumer *>( data );

    uint32_t activations = 0;
    Timer logTimer;

    // Only used for TRACE log level logging
    std::array<std::pair<uint32_t, uint32_t>, 8> lastFrameIds{}; // .first=can id, .second=counter
    uint8_t lastFrameIdPos = 0;
    uint32_t processedFramesCounter = 0;
    do
    {
        activations++;
        if ( consumer->shouldSleep() )
        {
            // We either just started or there was a decoder manifest update that we can't use.
            // We should sleep
            consumer->mLogger.trace( "NetworkChannelConsumer::doWork",
                                     "No valid decoding dictionary available, Consumer going to sleep " );
            // Wait here for the decoder Manifest to come.
            consumer->mWait.wait( Platform::Signal::WaitWithPredicate );
            // At this point, we should be able to see events coming as the channel is also
            // woken up.
        }
        // Below section utilize decoder dictionary to perform CAN message decoding and collection.
        // Use a Mutex to prevent updating decoder dictionary in the middle of CAN Frame processing.
        std::shared_ptr<const CANDecoderDictionary> decoderDictPtr;
        {
            std::lock_guard<std::mutex> lock( consumer->mDecoderDictMutex );
            decoderDictPtr = consumer->mDecoderDictionaryConstPtr;
        }

        // Pop any message from the Input Buffer
        CANRawMessage message;
        if ( consumer->mInputBuffer->pop( message ) )
        {
            TraceVariable traceQueue =
                static_cast<TraceVariable>( consumer->mCANChannelID + QUEUE_SOCKET_TO_CONSUMER_0 );
            TraceModule::get().setVariable(
                ( traceQueue < QUEUE_SOCKET_TO_CONSUMER_MAX ) ? traceQueue : QUEUE_SOCKET_TO_CONSUMER_MAX,
                consumer->mInputBuffer->read_available() + 1 );
            CANDecodedMessage decodedMessage;
            decodedMessage.mChannelProtocol = consumer->mChannelProtocol;
            decodedMessage.mChannelType = consumer->mType;
            decodedMessage.mChannelIfName = consumer->mIfName;
            decodedMessage.mReceptionTime = message.getReceptionTimestamp();
            decodedMessage.mFrameInfo.mFrameID = message.getMessageID();
            decodedMessage.mFrameInfo.mFrameRawData.assign(
                message.getMessageFrame().data(), message.getMessageDLC() + message.getMessageFrame().data() );
            // get decoderMethod from the decoder dictionary
            const auto &decoderMethod = decoderDictPtr->canMessageDecoderMethod;
            // a set of signalID specifying which signal to collect
            const auto &signalIDsToCollect = decoderDictPtr->signalIDsToCollect;
            // check if this CAN message ID on this CAN Channel has the decoder method
            if ( decoderMethod.find( consumer->mCANChannelID ) != decoderMethod.cend() &&
                 decoderMethod.at( consumer->mCANChannelID ).find( message.getMessageID() ) !=
                     decoderMethod.at( consumer->mCANChannelID ).cend() )
            {
                // format to be used for decoding
                const auto &format = decoderMethod.at( consumer->mCANChannelID ).at( message.getMessageID() ).format;
                const auto &collectType =
                    decoderMethod.at( consumer->mCANChannelID ).at( message.getMessageID() ).collectType;

                // Only used for TRACE log level logging
                if ( collectType == CANMessageCollectType::RAW ||
                     collectType == CANMessageCollectType::RAW_AND_DECODE ||
                     collectType == CANMessageCollectType::DECODE )
                {
                    bool found = false;
                    for ( auto &p : lastFrameIds )
                    {
                        if ( p.first == message.getMessageID() )
                        {
                            found = true;
                            p.second++;
                            break;
                        }
                    }
                    if ( !found )
                    {
                        lastFrameIdPos++;
                        if ( lastFrameIdPos >= lastFrameIds.size() )
                        {
                            lastFrameIdPos = 0;
                        }
                        lastFrameIds[lastFrameIdPos] = std::pair<uint32_t, uint32_t>( message.getMessageID(), 1 );
                    }
                    processedFramesCounter++;
                }

                // Check if we want to collect RAW CAN Frame; If so we also need to ensure Buffer is valid
                if ( consumer->mCANBufferPtr.get() != nullptr &&
                     ( collectType == CANMessageCollectType::RAW ||
                       collectType == CANMessageCollectType::RAW_AND_DECODE ) )
                {
                    // prepare the raw CAN Frame
                    struct CollectedCanRawFrame canRawFrame;
                    canRawFrame.frameID = message.getMessageID();
                    canRawFrame.channelId = consumer->mCANChannelID;
                    canRawFrame.receiveTime = message.getReceptionTimestamp();
                    // CollectedCanRawFrame only receive 8 CAN Raw Bytes
                    canRawFrame.size =
                        std::min( static_cast<uint8_t>( message.getMessageFrame().size() ), MAX_CAN_FRAME_BYTE_SIZE );
                    std::copy( message.getMessageFrame().begin(),
                               message.getMessageFrame().begin() + canRawFrame.size,
                               canRawFrame.data.begin() );
                    // Push raw CAN Frame to the Buffer for next stage to consume
                    // Note buffer is lock_free buffer and multiple Network Channel Instance could push data to it.
                    TraceModule::get().incrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_CAN );
                    if ( !consumer->mCANBufferPtr->push( canRawFrame ) )
                    {
                        TraceModule::get().decrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_CAN );
                        consumer->mLogger.warn( "NetworkChannelConsumer::doWork", "RAW CAN Frame Buffer Full! " );
                    }
                    else
                    {
                        // Enable below logging for debugging
                        // consumer->mLogger.trace( "NetworkChannelConsumer::doWork",
                        //                             "Collect RAW CAN Frame ID: " +
                        //                                 std::to_string( message.getMessageID() ) );
                    }
                }
                // check if we want to decode can frame into signals and collect signals
                if ( consumer->mSignalBufferPtr.get() != nullptr &&
                     ( collectType == CANMessageCollectType::DECODE ||
                       collectType == CANMessageCollectType::RAW_AND_DECODE ) )
                {
                    if ( format.isValid() )
                    {
                        if ( consumer->mCANDecoder->decodeCANMessage( message.getMessageFrame().data(),
                                                                      message.getMessageDLC(),
                                                                      format,
                                                                      signalIDsToCollect,
                                                                      decodedMessage ) )
                        {
                            for ( auto const &signal : decodedMessage.mFrameInfo.mSignals )
                            {
                                // Create Collected Signal Object
                                struct CollectedSignal collectedSignal(
                                    signal.mSignalID, decodedMessage.mReceptionTime, signal.mPhysicalValue );
                                // Push collected signal to the Signal Buffer
                                TraceModule::get().incrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                                if ( !consumer->mSignalBufferPtr->push( collectedSignal ) )
                                {
                                    TraceModule::get().decrementAtomicVariable( QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                                    consumer->mLogger.warn( "NetworkChannelConsumer::doWork", "Signal Buffer Full! " );
                                }
                                else
                                {
                                    // Enable below logging for debugging
                                    // consumer->mLogger.trace( "NetworkChannelConsumer::doWork",
                                    //                             "Acquire Signal ID: " +
                                    //                                 std::to_string( signal.mSignalID ) );
                                }
                            }
                        }
                        else
                        {
                            // The decoding was not fully successful
                            consumer->mLogger.warn( "NetworkChannelConsumer::doWork",
                                                    "CAN Frame " + std::to_string( message.getMessageID() ) +
                                                        " decoding failed! " );
                        }
                    }
                    else
                    {
                        // The CAN Message format is not valid, report as warning
                        consumer->mLogger.warn(
                            "NetworkChannelConsumer::doWork",
                            "CANMessageFormat Invalid for format message id: " + std::to_string( format.mMessageID ) +
                                " can message id: " + std::to_string( message.getMessageID() ) +
                                " on CAN Channel Id: " + std::to_string( consumer->mCANChannelID ) );
                    }
                }
            }
        }
        else
        {
            if ( logTimer.getElapsedMs().count() > static_cast<int64_t>( LoggingModule::LOG_AGGREGATION_TIME_MS ) )
            {
                // Nothing is in the ring buffer to consume. Go to idle mode for some time.
                std::stringstream logMessage;
                logMessage << "Channel Id: " << consumer->mCANChannelID
                           << ". Activations since last print: " << std::to_string( activations )
                           << ". Number of frames over all processed " << processedFramesCounter
                           << ".Last CAN IDs processed:";
                for ( auto id : lastFrameIds )
                {
                    logMessage << id.first << " (x " << id.second << "), ";
                }
                logMessage << ". Waiting for some data to come. Idling for :" + std::to_string( consumer->mIdleTime ) +
                                  " ms";
                consumer->mLogger.trace( "NetworkChannelConsumer::doWork", logMessage.str() );
                activations = 0;
                logTimer.reset();
            }
            consumer->mWait.wait( consumer->mIdleTime );
        }
    } while ( !consumer->shouldStop() );
}

bool
NetworkChannelConsumer::connect()
{
    if ( mInputBuffer.get() != nullptr && mSignalBufferPtr.get() != nullptr && mCANBufferPtr.get() != nullptr &&
         start() )
    {
        return true;
    }

    return false;
}

bool
NetworkChannelConsumer::disconnect()
{
    return stop();
}

bool
NetworkChannelConsumer::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
