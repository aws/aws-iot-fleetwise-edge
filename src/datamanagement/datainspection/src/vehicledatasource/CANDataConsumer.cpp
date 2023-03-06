// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CANDataConsumer.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <linux/can.h>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

CANDataConsumer::~CANDataConsumer()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

bool
CANDataConsumer::init( VehicleDataSourceID canChannelID, SignalBufferPtr signalBufferPtr, uint32_t idleTimeMs )
{
    mID = generateConsumerID();
    mCANDecoder = std::make_unique<CANDecoder>();
    if ( signalBufferPtr.get() == nullptr )
    {
        FWE_LOG_TRACE( "Init Failed due to bufferPtr as nullptr" );
        return false;
    }
    else
    {
        FWE_LOG_TRACE( "Init Network channel consumer with id: " + std::to_string( canChannelID ) );
        mDataSourceID = canChannelID;
        mSignalBufferPtr = signalBufferPtr;
    }
    if ( idleTimeMs != 0 )
    {
        mIdleTime = idleTimeMs;
    }
    return true;
}

void
CANDataConsumer::suspendDataConsumption()
{
    // Go back to sleep
    FWE_LOG_TRACE( "Going to sleep until a the resume signal. Consumer : " + std::to_string( mDataSourceID ) );
    mShouldSleep.store( true, std::memory_order_relaxed );
}

void
CANDataConsumer::resumeDataConsumption( ConstDecoderDictionaryConstPtr &dictionary )
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
            // TODO : This downcast is done three times in this entity. One time when the generic decoder is
            // provider and another one on every work iteration. As we plan to consolidate all decoding
            // rules for different data source types in one single decoder instance, this down cast
            // shall be removed.
            mDecoderDictionaryConstPtr = std::dynamic_pointer_cast<const CANDecoderDictionary>( dictionary );
        }
        if ( mDecoderDictionaryConstPtr != nullptr )
        {
            // Decoder dictionary
            auto decoderDictPtr = std::dynamic_pointer_cast<const CANDecoderDictionary>( mDecoderDictionaryConstPtr );
            const auto &decoderMethod = decoderDictPtr->canMessageDecoderMethod;
            std::string canIds;
            // check if this CAN message ID on this CAN Channel has the decoder method and if yes log the number
            if ( decoderMethod.find( mDataSourceID ) != decoderMethod.cend() )
            {
                for ( auto &decode : decoderMethod.at( mDataSourceID ) )
                {
                    canIds += std::to_string( decode.first ) + ", ";
                }
            }
            FWE_LOG_TRACE( "Changing Decoder Dictionary on Consumer :" + std::to_string( mID ) +
                           " with decoding rules for CAN-IDs: " + canIds );
            // Make sure the thread does not sleep anymore
            mShouldSleep.store( false );
            // Wake up the worker thread.
            mWait.notify();
        }
        else
        {
            FWE_LOG_ERROR( "Received invalid decoder dictionary :" + std::to_string( mID ) );
        }
    }
}

bool
CANDataConsumer::start()
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
        mThread.setThreadName( "fwDIConsumer" + std::to_string( mID ) );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
CANDataConsumer::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Thread stopped" );
    return !mThread.isActive();
}

bool
CANDataConsumer::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
CANDataConsumer::shouldSleep() const
{
    return mShouldSleep.load( std::memory_order_relaxed );
}

bool
CANDataConsumer::findDecoderMethod( uint32_t &messageId,
                                    const CANDecoderDictionary::CANMsgDecoderMethodType &decoderMethod,
                                    CANDecodedMessage &decodedMessage,
                                    CANMessageDecoderMethod &currentMessageDecoderMethod )
{
    auto outerMapIt = decoderMethod.find( mDataSourceID );
    if ( outerMapIt != decoderMethod.cend() )
    {
        auto it = outerMapIt->second.find( messageId );

        if ( it != outerMapIt->second.cend() )
        {
            currentMessageDecoderMethod = it->second;
            return true;
        }
        it = outerMapIt->second.find( messageId & CAN_EFF_MASK );

        if ( it != outerMapIt->second.cend() )
        {
            messageId = messageId & CAN_EFF_MASK;
            currentMessageDecoderMethod = it->second;
            decodedMessage.mFrameInfo.mFrameID = messageId;
            return true;
        }
    }
    return false;
}

void
CANDataConsumer::doWork( void *data )
{

    CANDataConsumer *consumer = static_cast<CANDataConsumer *>( data );

    uint32_t activations = 0;
    Timer logTimer;

    // Only used for TRACE log level logging
    std::array<std::pair<uint32_t, uint32_t>, 8> lastFrameIds{}; // .first=can id, .second=counter
    uint8_t lastFrameIdPos = 0;
    uint32_t processedFramesCounter = 0;

    TraceSection traceSection =
        ( ( consumer->mDataSourceID < static_cast<VehicleDataSourceID>( toUType( TraceSection::CAN_DECODER_CYCLE_19 ) -
                                                                        toUType( TraceSection::CAN_DECODER_CYCLE_0 ) ) )
              ? static_cast<TraceSection>( consumer->mDataSourceID + toUType( TraceSection::CAN_DECODER_CYCLE_0 ) )
              : TraceSection::CAN_DECODER_CYCLE_19 );
    do
    {
        activations++;
        if ( consumer->shouldSleep() )
        {
            // We either just started or there was a decoder manifest update that we can't use.
            // We should sleep
            FWE_LOG_TRACE( "No valid decoding dictionary available, Consumer going to sleep" );
            // Wait here for the decoder Manifest to come.
            consumer->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
            // At this point, we should be able to see events coming as the channel is also
            // woken up.
            TraceModule::get().sectionBegin( traceSection );
        }
        // Below section utilize decoder dictionary to perform CAN message decoding and collection.
        // Use a Mutex to prevent updating decoder dictionary in the middle of CAN Frame processing.
        std::shared_ptr<const CANDecoderDictionary> decoderDictPtr;
        {
            std::lock_guard<std::mutex> lock( consumer->mDecoderDictMutex );
            decoderDictPtr =
                std::dynamic_pointer_cast<const CANDecoderDictionary>( consumer->mDecoderDictionaryConstPtr );
        }

        // Pop any message from the Input Buffer
        VehicleDataMessage message;
        if ( consumer->mInputBufferPtr->pop( message ) )
        {
            TraceVariable traceQueue = static_cast<TraceVariable>(
                consumer->mDataSourceID + toUType( TraceVariable::QUEUE_SOCKET_TO_CONSUMER_0 ) );
            TraceModule::get().setVariable( ( traceQueue < TraceVariable::QUEUE_SOCKET_TO_CONSUMER_19 )
                                                ? traceQueue
                                                : TraceVariable::QUEUE_SOCKET_TO_CONSUMER_19,
                                            consumer->mInputBufferPtr->read_available() + 1 );
            CANDecodedMessage decodedMessage;
            decodedMessage.mChannelProtocol = consumer->mDataSourceProtocol;
            decodedMessage.mChannelType = consumer->mType;
            decodedMessage.mChannelIfName = consumer->mIfName;
            decodedMessage.mReceptionTime = message.getReceptionTimestamp();
            decodedMessage.mFrameInfo.mFrameID = static_cast<uint32_t>( message.getMessageID() );

            const auto messageRawData = message.getRawData().data();
            if ( messageRawData != nullptr )
            {
                decodedMessage.mFrameInfo.mFrameRawData.assign( messageRawData,
                                                                message.getRawData().size() + messageRawData );
            }
            // get decoderMethod from the decoder dictionary
            const auto &decoderMethod = decoderDictPtr->canMessageDecoderMethod;
            // a set of signalID specifying which signal to collect
            const auto &signalIDsToCollect = decoderDictPtr->signalIDsToCollect;

            // check if this CAN message ID on this CAN Channel has the decoder method
            uint32_t messageId = static_cast<uint32_t>( message.getMessageID() );
            CANMessageDecoderMethod currentMessageDecoderMethod;

            // The value of messageId may be changed by the findDecoderMethod function. This is a
            // workaround as the cloud as of now does not send extended id messages.
            // If the decoder method for this message is not found in
            // decoderMethod dictionary, we check for the same id without the MSB set.
            // The message id which has a decoderMethod gets passed into messageId

            bool hasDecoderMethod =
                consumer->findDecoderMethod( messageId, decoderMethod, decodedMessage, currentMessageDecoderMethod );

            if ( hasDecoderMethod )
            {
                // format to be used for decoding
                const auto &format = currentMessageDecoderMethod.format;
                const auto &collectType = currentMessageDecoderMethod.collectType;

                // Only used for TRACE log level logging
                if ( ( collectType == CANMessageCollectType::RAW ) ||
                     ( collectType == CANMessageCollectType::RAW_AND_DECODE ) ||
                     ( collectType == CANMessageCollectType::DECODE ) )
                {
                    bool found = false;
                    for ( auto &p : lastFrameIds )
                    {
                        if ( p.first == messageId )
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
                        lastFrameIds[lastFrameIdPos] = std::pair<uint32_t, uint32_t>( messageId, 1 );
                    }
                    processedFramesCounter++;
                }

                // Check if we want to collect RAW CAN Frame; If so we also need to ensure Buffer is valid
                if ( ( consumer->mCANBufferPtr.get() != nullptr ) &&
                     ( ( collectType == CANMessageCollectType::RAW ) ||
                       ( collectType == CANMessageCollectType::RAW_AND_DECODE ) ) )
                {
                    // prepare the raw CAN Frame
                    struct CollectedCanRawFrame canRawFrame;
                    canRawFrame.frameID = messageId;
                    canRawFrame.channelId = consumer->mDataSourceID;
                    canRawFrame.receiveTime = message.getReceptionTimestamp();
                    // CollectedCanRawFrame receive up to 64 CAN Raw Bytes
                    canRawFrame.size =
                        std::min( static_cast<uint8_t>( message.getRawData().size() ), MAX_CAN_FRAME_BYTE_SIZE );
                    std::copy( message.getRawData().begin(),
                               message.getRawData().begin() + canRawFrame.size,
                               canRawFrame.data.begin() );
                    // Push raw CAN Frame to the Buffer for next stage to consume
                    // Note buffer is lock_free buffer and multiple Vehicle Data Source Instance could push
                    // data to it.
                    TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_CAN );
                    if ( !consumer->mCANBufferPtr->push( canRawFrame ) )
                    {
                        TraceModule::get().decrementAtomicVariable(
                            TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_CAN );
                        FWE_LOG_WARN( "RAW CAN Frame Buffer Full" );
                    }
                    else
                    {
                        // Enable below logging for debugging
                        // FWE_LOG_TRACE( "CANDataConsumer::doWork",
                        //               "Collect RAW CAN Frame ID: " +
                        //                   std::to_string( static_cast<uint32_t>( messageId ) ) );
                    }
                }
                // check if we want to decode can frame into signals and collect signals
                if ( ( consumer->mSignalBufferPtr.get() != nullptr ) &&
                     ( ( collectType == CANMessageCollectType::DECODE ) ||
                       ( collectType == CANMessageCollectType::RAW_AND_DECODE ) ) )
                {
                    if ( format.isValid() )
                    {
                        if ( consumer->mCANDecoder->decodeCANMessage( message.getRawData().data(),
                                                                      message.getRawData().size(),
                                                                      format,
                                                                      signalIDsToCollect,
                                                                      decodedMessage ) )
                        {
                            for ( auto const &signal : decodedMessage.mFrameInfo.mSignals )
                            {
                                // Create Collected Signal Object
                                struct CollectedSignal collectedSignal;
                                const auto signalType = signal.mSignalType;
                                switch ( signalType )
                                {
                                case SignalType::UINT64:
                                    collectedSignal = CollectedSignal{ signal.mSignalID,
                                                                       decodedMessage.mReceptionTime,
                                                                       signal.mPhysicalValue.signalValue.uint64Val,
                                                                       signal.mSignalType };
                                    break;
                                case SignalType::INT64:
                                    collectedSignal = CollectedSignal{ signal.mSignalID,
                                                                       decodedMessage.mReceptionTime,
                                                                       signal.mPhysicalValue.signalValue.int64Val,
                                                                       signal.mSignalType };
                                    break;
                                default:
                                    collectedSignal = CollectedSignal{ signal.mSignalID,
                                                                       decodedMessage.mReceptionTime,
                                                                       signal.mPhysicalValue.signalValue.doubleVal,
                                                                       signal.mSignalType };
                                    break;
                                }

                                // Push collected signal to the Signal Buffer
                                TraceModule::get().incrementAtomicVariable(
                                    TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                                if ( !consumer->mSignalBufferPtr->push( collectedSignal ) )
                                {
                                    TraceModule::get().decrementAtomicVariable(
                                        TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                                    FWE_LOG_WARN( "Signal Buffer Full" );
                                }
                                else
                                {
                                    // Enable below logging for debugging
                                    // FWE_LOG_TRACE( "CANDataConsumer::doWork",
                                    //               "Acquire Signal ID: " + std::to_string( signal.mSignalID ) );
                                }
                            }
                        }
                        else
                        {
                            // The decoding was not fully successful
                            FWE_LOG_WARN( "CAN Frame " + std::to_string( messageId ) + " decoding failed" );
                        }
                    }
                    else
                    {
                        // The CAN Message format is not valid, report as warning
                        FWE_LOG_WARN(

                            "CANMessageFormat Invalid for format message id: " + std::to_string( format.mMessageID ) +
                            " can message id: " + std::to_string( messageId ) +
                            " on CAN Channel Id: " + std::to_string( consumer->mDataSourceID ) );
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
                logMessage << "Channel Id: " << consumer->mDataSourceID
                           << ". Activations since last print: " << std::to_string( activations )
                           << ". Number of frames over all processed " << processedFramesCounter
                           << ".Last CAN IDs processed:";
                for ( auto id : lastFrameIds )
                {
                    logMessage << id.first << " (x " << id.second << "), ";
                }
                logMessage << ". Waiting for some data to come. Idling for :" + std::to_string( consumer->mIdleTime ) +
                                  " ms";
                FWE_LOG_TRACE( logMessage.str() );
                activations = 0;
                logTimer.reset();
            }
            TraceModule::get().sectionEnd( traceSection );
            consumer->mWait.wait( consumer->mIdleTime );
            TraceModule::get().sectionBegin( traceSection );
        }
    } while ( !consumer->shouldStop() );
}

bool
CANDataConsumer::connect()
{
    if ( ( mInputBufferPtr.get() != nullptr ) && ( mSignalBufferPtr.get() != nullptr ) &&
         ( mCANBufferPtr.get() != nullptr ) && start() )
    {
        return true;
    }

    return false;
}

bool
CANDataConsumer::disconnect()
{
    return stop();
}

bool
CANDataConsumer::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
