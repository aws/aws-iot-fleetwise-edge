// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionInspectionWorkerThread.h"
#include "TraceModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

bool
CollectionInspectionWorkerThread::init( const std::shared_ptr<SignalBuffer> &inputSignalBufferIn,
                                        const std::shared_ptr<CANBuffer> &inputCANBufferIn,
                                        const std::shared_ptr<ActiveDTCBuffer> &inputActiveDTCBuffer,
                                        const std::shared_ptr<CollectedDataReadyToPublish> &outputCollectedDataIn,
                                        uint32_t idleTimeMs,
                                        bool dataReductionProbabilityDisabled )
{
    fInputSignalBuffer = inputSignalBufferIn;
    fInputCANBuffer = inputCANBufferIn;
    fInputActiveDTCBuffer = inputActiveDTCBuffer;
    fOutputCollectedData = outputCollectedDataIn;
    if ( idleTimeMs != 0 )
    {
        fIdleTimeMs = idleTimeMs;
    }
    fCollectionInspectionEngine.setDataReductionParameters( dataReductionProbabilityDisabled );

    return true;
}

bool
CollectionInspectionWorkerThread::start()
{
    if ( ( fInputCANBuffer == nullptr ) || ( fInputCANBuffer == nullptr ) || ( fInputActiveDTCBuffer == nullptr ) ||
         ( fOutputCollectedData == nullptr ) )
    {
        fLogger.error( "CollectionInspectionWorkerThread::start",
                       "Collection Engine cannot be started without correct configurations" );
        return false;
    }
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( fThreadMutex );
    // On multi core systems the shared variable fShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    fShouldStop.store( false );
    if ( !fThread.create( doWork, this ) )
    {
        fLogger.trace( "CollectionInspectionWorkerThread::start", "Inspection Thread failed to start" );
    }
    else
    {
        fLogger.trace( "CollectionInspectionWorkerThread::start", "Inspection Thread started" );
        fThread.setThreadName( "fwDICollInsEng" );
    }

    return fThread.isActive() && fThread.isValid();
}

bool
CollectionInspectionWorkerThread::stop()
{
    if ( ( !fThread.isValid() ) || ( !fThread.isActive() ) )
    {
        return true;
    }
    std::lock_guard<std::mutex> lock( fThreadMutex );
    fShouldStop.store( true, std::memory_order_relaxed );
    fLogger.trace( "CollectionInspectionWorkerThread::stop", "Request stop" );
    fWait.notify();
    fThread.release();
    fLogger.trace( "CollectionInspectionWorkerThread::stop", "Stop finished" );
    fShouldStop.store( false, std::memory_order_relaxed );
    return !fThread.isActive();
}

bool
CollectionInspectionWorkerThread::shouldStop() const
{
    return fShouldStop.load( std::memory_order_relaxed );
}

void
CollectionInspectionWorkerThread::onChangeInspectionMatrix(
    const std::shared_ptr<const InspectionMatrix> &activeConditions )
{
    {
        std::lock_guard<std::mutex> lock( fInspectionMatrixMutex );
        fUpdatedInspectionMatrix = activeConditions;
        fUpdatedInspectionMatrixAvailable = true;
        fLogger.trace( "CollectionInspectionWorkerThread::onChangeInspectionMatrix",
                       "New inspection matrix handed over" );
        // Wake up the thread.
        fWait.notify();
    }
}

void
CollectionInspectionWorkerThread::onNewDataAvailable()
{
    fWait.notify();
}

void
CollectionInspectionWorkerThread::doWork( void *data )
{

    CollectionInspectionWorkerThread *consumer = static_cast<CollectionInspectionWorkerThread *>( data );
    Timestamp lastInputTimeEvaluated = 0;
    TimePoint lastTimeEvaluated = { 0, 0 };
    Timestamp lastTraceOutput = 0;
    uint64_t inputCounterSinceLastEvaluate = 0;
    uint32_t statisticInputMessagesProcessed = 0;
    uint32_t statisticDataSentOut = 0;
    uint32_t activations = 0;
    do
    {
        activations++;
        if ( consumer->fUpdatedInspectionMatrixAvailable )
        {
            std::shared_ptr<const InspectionMatrix> newInspectionMatrix;
            {
                std::lock_guard<std::mutex> lock( consumer->fInspectionMatrixMutex );
                consumer->fUpdatedInspectionMatrixAvailable = false;
                newInspectionMatrix = consumer->fUpdatedInspectionMatrix;
            }
            consumer->fCollectionInspectionEngine.onChangeInspectionMatrix( newInspectionMatrix );
        }
        // Only run the main inspection loop if there is an inspection matrix
        // Otherwise, go to sleep.
        if ( consumer->fUpdatedInspectionMatrix )
        {
            bool readyToSleep = true;
            Timestamp latestSignalTime = 0;
            CollectedSignal inputSignal( 0, 0, 0.0 );
            std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> buf = {};
            CollectedCanRawFrame inputCANFrame( 0, 0, 0, buf, 0 );
            TimePoint currentTime = consumer->fClock->timeSinceEpoch();
            // Consume any new signals and pass them over to the inspection Engine
            if ( consumer->fInputSignalBuffer->pop( inputSignal ) )
            {
                TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS );
                TraceModule::get().incrementVariable( TraceVariable::CE_PROCESSED_SIGNALS );
                readyToSleep = false;
                consumer->fCollectionInspectionEngine.addNewSignal(
                    inputSignal.signalID,
                    consumer->calculateMonotonicTime( currentTime, inputSignal.receiveTime ),
                    inputSignal.value );
                latestSignalTime = std::max( latestSignalTime, inputSignal.receiveTime );
                inputCounterSinceLastEvaluate++;
                statisticInputMessagesProcessed++;
            }
            // Consume any raw frames
            if ( consumer->fInputCANBuffer->pop( inputCANFrame ) )
            {
                TraceModule::get().decrementAtomicVariable( TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_CAN );
                TraceModule::get().incrementVariable( TraceVariable::CE_PROCESSED_CAN_FRAMES );
                readyToSleep = false;
                consumer->fCollectionInspectionEngine.addNewRawCanFrame(
                    inputCANFrame.frameID,
                    inputCANFrame.channelId,
                    consumer->calculateMonotonicTime( currentTime, inputCANFrame.receiveTime ),
                    inputCANFrame.data,
                    inputCANFrame.size );
                latestSignalTime = std::max( latestSignalTime, inputCANFrame.receiveTime );
                inputCounterSinceLastEvaluate++;
                statisticInputMessagesProcessed++;
            }

            // Consume any Active DTCs
            // We could check if the DTCs have changed here, but not necessary
            // as we are looking at only the latest known DTCs.
            // We only pop one item from the Buffer for a reason : DTCs represent
            // the health of all ECUs in the network. The Inspection Engine does
            // not need to know that topology and thus counts on the OBD Module
            // to aggregate all DTCs from all ECUs in one single Item.
            DTCInfo activeDTCs = {};
            if ( consumer->fInputActiveDTCBuffer->pop( activeDTCs ) )
            {
                consumer->fCollectionInspectionEngine.setActiveDTCs( activeDTCs );
            }

            // Trigger inspection on whatever that has been consumed.
            if ( ( ( latestSignalTime - lastInputTimeEvaluated ) >= EVALUATE_INTERVAL_MS ) ||
                 ( inputCounterSinceLastEvaluate >= 256 ) )
            {
                lastInputTimeEvaluated = latestSignalTime;
                lastTimeEvaluated = consumer->fClock->timeSinceEpoch();
                consumer->fCollectionInspectionEngine.evaluateConditions( lastTimeEvaluated );
                inputCounterSinceLastEvaluate = 0;
            }

            // before going to sleep do another evaluation if last evaluation is more than EVALUATE_INTERVAL_MS ago
            if ( readyToSleep && ( ( consumer->fClock->monotonicTimeSinceEpochMs() -
                                     lastTimeEvaluated.monotonicTimeMs ) >= EVALUATE_INTERVAL_MS ) )
            {
                lastInputTimeEvaluated = latestSignalTime;
                lastTimeEvaluated = consumer->fClock->timeSinceEpoch();
                consumer->fCollectionInspectionEngine.evaluateConditions( lastTimeEvaluated );
                inputCounterSinceLastEvaluate = 0;
            }
            uint32_t waitTimeMs = consumer->fIdleTimeMs;
            std::shared_ptr<const TriggeredCollectionSchemeData> collectedData =
                consumer->fCollectionInspectionEngine.collectNextDataToSend( consumer->fClock->timeSinceEpoch(),
                                                                             waitTimeMs );
            while ( ( collectedData != nullptr ) && ( !consumer->shouldStop() ) )
            {
                TraceModule::get().incrementVariable( TraceVariable::CE_TRIGGERS );
                if ( !consumer->fOutputCollectedData->push( collectedData ) )
                {
                    consumer->fLogger.warn( "CollectionInspectionWorkerThread::doWork",
                                            "Collected data output buffer is full" );
                }
                else
                {
                    statisticDataSentOut++;
                    consumer->notifyListeners<>( &IDataReadyToPublishListener::onDataReadyToPublish );
                }
                collectedData = consumer->fCollectionInspectionEngine.collectNextDataToSend(
                    consumer->fClock->timeSinceEpoch(), waitTimeMs );
            }

            if ( readyToSleep )
            {
                // Nothing is in the ring buffer to consume. Go to idle mode for some time.
                uint32_t timeToWait = std::min( waitTimeMs, consumer->fIdleTimeMs );
                // Print only every THREAD_IDLE_TIME_MS to avoid console spam
                if ( consumer->fClock->monotonicTimeSinceEpochMs() >
                     ( lastTraceOutput + LoggingModule::LOG_AGGREGATION_TIME_MS ) )
                {
                    consumer->fLogger.trace(
                        "CollectionInspectionWorkerThread::doWork",
                        "Activations: " + std::to_string( activations ) +
                            ". Waiting for some data to come. Idling for :" + std::to_string( timeToWait ) +
                            " ms or until notify. Since last idling processed " +
                            std::to_string( statisticInputMessagesProcessed ) +
                            " incoming data packages and sent out " + std::to_string( statisticDataSentOut ) +
                            " packages out" );
                    activations = 0;
                    statisticInputMessagesProcessed = 0;
                    statisticDataSentOut = 0;
                    lastTraceOutput = consumer->fClock->monotonicTimeSinceEpochMs();
                }
                consumer->fWait.wait( timeToWait );
            }
        }
        else
        {
            // No inspection Matrix available. Wait for it from the CollectionScheme manager
            consumer->fWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        }
    } while ( !consumer->shouldStop() );
}

TimePoint
CollectionInspectionWorkerThread::calculateMonotonicTime( const TimePoint &currTime, Timestamp systemTimeMs )
{
    TimePoint convertedTime = timePointFromSystemTime( currTime, systemTimeMs );
    if ( ( convertedTime.systemTimeMs == 0 ) && ( convertedTime.monotonicTimeMs == 0 ) )
    {
        fLogger.error( "CollectionInspectionWorkerThread::timePointFromSystemTime",
                       "The system time " + std::to_string( systemTimeMs ) +
                           " corresponds to a time in the past before the monotonic" +
                           " clock started ticking. Current system time: " + std::to_string( currTime.systemTimeMs ) +
                           ". Current monotonic time: " + std::to_string( currTime.monotonicTimeMs ) );
        return TimePoint{ systemTimeMs, 0 };
    }
    return convertedTime;
}

bool
CollectionInspectionWorkerThread::isAlive()
{
    return fThread.isValid() && fThread.isActive();
}

CollectionInspectionWorkerThread::~CollectionInspectionWorkerThread()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
