// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionInspectionEngine.h"
#include "ClockHandler.h"
#include "TraceModule.h"
#include <algorithm>
#include <iostream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

CollectionInspectionEngine::CollectionInspectionEngine( bool sendDataOnlyOncePerCondition )
    : mSendDataOnlyOncePerCondition( sendDataOnlyOncePerCondition )
{
    setActiveDTCsConsumed( ALL_CONDITIONS, false );
}

CollectionInspectionEngine::SignalHistoryBuffer &
CollectionInspectionEngine::addSignalToBuffer( const InspectionMatrixSignalCollectionInfo &signal )
{

    for ( auto &buffer : mSignalBuffers[signal.signalID] )
    {
        if ( buffer.mMinimumSampleIntervalMs == signal.minimumSampleIntervalMs )
        {
            buffer.mSize = std::max( buffer.mSize, signal.sampleBufferSize );
            return buffer;
        }
    }
    // No entry with same sample interval found
    mSignalBuffers[signal.signalID].emplace_back( signal.sampleBufferSize, signal.minimumSampleIntervalMs );
    return mSignalBuffers[signal.signalID].back();
}

bool
CollectionInspectionEngine::isSignalPartOfEval( const struct ExpressionNode *expression,
                                                InspectionSignalID signalID,
                                                int remainingStackDepth )
{
    if ( ( remainingStackDepth <= 0 ) || ( expression == nullptr ) )
    {
        return false;
    }
    if ( ( expression->nodeType == ExpressionNodeType::SIGNAL ) ||
         ( expression->nodeType == ExpressionNodeType::WINDOWFUNCTION ) )
    {
        return expression->signalID == signalID;
    }
    else if ( expression->nodeType == ExpressionNodeType::GEOHASHFUNCTION )
    {
        return ( expression->function.geohashFunction.latitudeSignalID == signalID ) ||
               ( expression->function.geohashFunction.longitudeSignalID == signalID );
    }
    // Recursion limited depth through last parameter
    bool leftRet = isSignalPartOfEval( expression->left, signalID, remainingStackDepth - 1 );
    bool rightRet = isSignalPartOfEval( expression->right, signalID, remainingStackDepth - 1 );
    return leftRet || rightRet;
}

void
CollectionInspectionEngine::onChangeInspectionMatrix(
    const std::shared_ptr<const InspectionMatrix> &activeInspectionMatrix )
{
    // Clears everything in this class including all data in the signal history buffer
    clear();
    mActiveInspectionMatrix = activeInspectionMatrix; // Pointers and references into this memory are maintained so hold
                                                      // a shared_ptr to it so it does not get deleted
    mConditionsNotTriggeredWaitingPublished.set();
    for ( auto &p : mActiveInspectionMatrix->conditions )
    {
        // Check if we can add an additional condition to mConditions
        if ( mConditions.size() >= MAX_NUMBER_OF_ACTIVE_CONDITION )
        {
            TraceModule::get().incrementVariable( TraceVariable::CE_TOO_MANY_CONDITIONS );
            mLogger.warn( "CollectionInspectionEngine::onChangeInspectionMatrix",
                          "Too many conditions are active. Up to " +
                              std::to_string( MAX_NUMBER_OF_ACTIVE_CONDITION - 1 ) +
                              " conditions can be active at a time. Additional conditions will be skipped" );
            break;
        }
        mConditions.emplace_back( p );
        if ( p.signals.size() > MAX_DIFFERENT_SIGNAL_IDS )
        {
            TraceModule::get().incrementVariable( TraceVariable::CE_SIGNAL_ID_OUTBOUND );
            mLogger.error( "CollectionInspectionEngine::onChangeInspectionMatrix",
                           "There can be only " + std::to_string( MAX_DIFFERENT_SIGNAL_IDS ) +
                               " different signal IDs" );
            return;
        }
        for ( auto &s : p.signals )
        {
            if ( s.signalID == INVALID_SIGNAL_ID )
            {
                mLogger.error( "CollectionInspectionEngine::onChangeInspectionMatrix",
                               "A SignalID with value" + std::to_string( INVALID_SIGNAL_ID ) + " is not allowed" );
                return;
            }
            if ( s.sampleBufferSize == 0 )
            {
                TraceModule::get().incrementVariable( TraceVariable::CE_SAMPLE_SIZE_ZERO );
                mLogger.error( "CollectionInspectionEngine::onChangeInspectionMatrix",
                               "A Sample buffer size of 0 is not allowed" );
                return;
            }
            SignalHistoryBuffer &buf = addSignalToBuffer( s );
            buf.addFixedWindow( s.fixedWindowPeriod );
        }
        for ( auto &c : p.canFrames )
        {
            bool found = false;
            for ( auto &buf : mCanFrameBuffers )
            {
                if ( ( buf.mFrameID == c.frameID ) && ( buf.mChannelID == c.channelID ) &&
                     ( buf.mMinimumSampleIntervalMs == c.minimumSampleIntervalMs ) )
                {
                    found = true;
                    buf.mSize = std::max( buf.mSize, c.sampleBufferSize );
                    break;
                }
            }
            if ( !found )
            {
                mCanFrameBuffers.emplace_back( c.frameID, c.channelID, c.sampleBufferSize, c.minimumSampleIntervalMs );
            }
        }
    }

    // At this point all buffers should be resized to correct size. Now pointer to std::vector elements can be used
    for ( size_t conditionIndex = 0; conditionIndex < mConditions.size(); conditionIndex++ )
    {
        auto &ac = mConditions[conditionIndex];
        for ( auto &s : ac.mCondition.signals )
        {
            SignalHistoryBuffer *buf = nullptr;
            for ( auto &buffer : mSignalBuffers[s.signalID] )
            {
                if ( buffer.mMinimumSampleIntervalMs == s.minimumSampleIntervalMs )
                {
                    buf = &buffer;
                    break;
                }
            }
            if ( ( buf != nullptr ) && isSignalPartOfEval( ac.mCondition.condition, s.signalID, MAX_EQUATION_DEPTH ) )
            {
                buf->mConditionsThatEvaluateOnThisSignal.set( conditionIndex );
                ac.mEvaluationSignals[s.signalID] = buf;
                FixedTimeWindowFunctionData *window = buf->getFixedWindow( s.fixedWindowPeriod );
                if ( window != nullptr )
                {
                    ac.mEvaluationFunctions[s.signalID] = window;
                }
            }
        }
    }

    // Assume all conditions are currently true;
    mConditionsWithConditionCurrentlyTrue.set();

    (void)preAllocateBuffers();
}
bool
CollectionInspectionEngine::preAllocateBuffers()
{
    // Allocate size
    uint32_t usedBytes = 0;
    // Allocate Signal Buffer
    for ( auto &bufferVector : mSignalBuffers )
    {
        // Go trough different sample intervals
        for ( auto &signal : bufferVector.second )
        {
            uint64_t requiredBytes = signal.mSize * static_cast<uint64_t>( sizeof( struct SignalSample ) );
            if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
            {
                mLogger.warn( "CollectionInspectionEngine::preAllocateBuffers",
                              "The requested " + std::to_string( signal.mSize ) +
                                  " number of signal samples leads to a memory requirement  that's above the maximum "
                                  "configured of " +
                                  std::to_string( MAX_SAMPLE_MEMORY ) + "Bytes" );
                signal.mSize = 0;
                return false;
            }
            usedBytes += static_cast<uint32_t>( requiredBytes );

            // reserve the size like new[]
            signal.mBuffer.resize( signal.mSize );
        }
    }
    // Allocate Can buffer
    for ( auto &buf : mCanFrameBuffers )
    {
        uint64_t requiredBytes = buf.mSize * static_cast<uint64_t>( sizeof( struct CanFrameSample ) );
        if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
        {
            mLogger.warn( "CollectionInspectionEngine::preAllocateBuffers",
                          "The requested " + std::to_string( buf.mSize ) +
                              " number of CAN raw samples leads to a memory requirement  that's above the maximum "
                              "configured of" +
                              std::to_string( MAX_SAMPLE_MEMORY ) + "Bytes" );
            buf.mSize = 0;
            return false;
        }
        usedBytes += static_cast<uint32_t>( requiredBytes );

        // reserve the size like new[]
        buf.mBuffer.resize( buf.mSize );
    }
    return true;
}

void
CollectionInspectionEngine::clear()
{
    mSignalBuffers.clear();
    mCanFrameBuffers.clear();
    mConditions.clear();
    mNextConditionToCollectedIndex = 0;
    mNextWindowFunctionTimesOut = 0;
    mConditionsWithInputSignalChanged.reset();
    mConditionsWithConditionCurrentlyTrue.reset();
    mConditionsNotTriggeredWaitingPublished.reset();
}

void
CollectionInspectionEngine::updateAllFixedWindowFunctions( InspectionTimestamp timestamp )
{
    mNextWindowFunctionTimesOut = std::numeric_limits<InspectionTimestamp>::max();
    for ( auto &signalVector : mSignalBuffers )
    {
        for ( auto &signal : signalVector.second )
        {
            for ( auto &functionWindow : signal.mWindowFunctionData )
            {
                bool changed = functionWindow.updateWindow( timestamp, mNextWindowFunctionTimesOut );
                if ( changed )
                {
                    mConditionsWithInputSignalChanged |= signal.mConditionsThatEvaluateOnThisSignal;
                }
            }
        }
    }
}

bool
CollectionInspectionEngine::evaluateConditions( const TimePoint &currentTime )
{
    bool oneConditionIsTrue = false;
    // if any sampling window times out there is a new value available to be processed by a condition
    if ( currentTime.monotonicTimeMs >= mNextWindowFunctionTimesOut )
    {
        updateAllFixedWindowFunctions( currentTime.monotonicTimeMs );
    }
    auto conditionsToEvaluate = ( mConditionsWithConditionCurrentlyTrue | mConditionsWithInputSignalChanged ) &
                                mConditionsNotTriggeredWaitingPublished;
    if ( conditionsToEvaluate.none() )
    {
        // No conditions to evaluate
        return false;
    }
    // faster implementation like find next bit set to one would be possible but for example
    // conditionsToEvaluate._Find_first is not part of C++ standard
    for ( uint32_t i = 0; i < mConditions.size(); i++ )
    {
        if ( conditionsToEvaluate.test( i ) )
        {
            ActiveCondition &condition = mConditions[i];
            if ( ( ( condition.mLastTrigger.systemTimeMs == 0 ) && ( condition.mLastTrigger.monotonicTimeMs == 0 ) ) ||
                 ( currentTime.monotonicTimeMs >=
                   condition.mLastTrigger.monotonicTimeMs + condition.mCondition.minimumPublishIntervalMs ) )
            {
                InspectionValue result = 0;
                bool resultBool = false;
                mConditionsWithInputSignalChanged.reset( i );
                ExpressionErrorCode ret =
                    eval( condition.mCondition.condition, condition, result, resultBool, MAX_EQUATION_DEPTH );
                if ( ( ret == ExpressionErrorCode::SUCCESSFUL ) && resultBool )
                {
                    if ( ( !condition.mCondition.triggerOnlyOnRisingEdge ) ||
                         ( !mConditionsWithConditionCurrentlyTrue.test( i ) ) )
                    {
                        mConditionsNotTriggeredWaitingPublished.reset( i );
                        condition.mLastTrigger = currentTime;
                    }
                    mConditionsWithConditionCurrentlyTrue.set( i );
                    oneConditionIsTrue = true;
                }
                else
                {
                    mConditionsWithConditionCurrentlyTrue.reset( i );
                }
            }
        }
    }
    return oneConditionIsTrue;
}

void
CollectionInspectionEngine::collectLastSignals( InspectionSignalID id,
                                                uint32_t minimumSamplingInterval,
                                                uint32_t maxNumberOfSignalsToCollect,
                                                uint32_t conditionId,
                                                InspectionTimestamp &newestSignalTimestamp,
                                                std::vector<CollectedSignal> &output )
{
    if ( mSignalBuffers[id].empty() )
    {
        // Signal not collected by any active condition
        return;
    }
    // Iterate through all sampling intervals of the signal
    for ( auto &buf : mSignalBuffers[id] )
    {
        if ( ( buf.mMinimumSampleIntervalMs == minimumSamplingInterval ) && ( buf.mSize > 0 ) )
        {
            int pos = static_cast<int>( buf.mCurrentPosition );
            for ( uint32_t i = 0; i < std::min( maxNumberOfSignalsToCollect, buf.mCounter ); i++ )
            {
                // Ensure access is in bounds
                if ( pos < 0 )
                {
                    pos = static_cast<int>( buf.mSize ) - 1;
                }
                if ( pos >= static_cast<int>( buf.mSize ) )
                {
                    pos = 0;
                }
                auto &sample = buf.mBuffer[static_cast<uint32_t>( pos )];
                if ( ( !sample.isAlreadyConsumed( conditionId ) ) || ( !mSendDataOnlyOncePerCondition ) )
                {
                    output.emplace_back( id, sample.mTimestamp, sample.mValue );
                    sample.setAlreadyConsumed( conditionId, true );
                }
                newestSignalTimestamp = std::max( newestSignalTimestamp, sample.mTimestamp );
                pos--;
            }
            return;
        }
    }
}

void
CollectionInspectionEngine::collectLastCanFrames( CANRawFrameID canID,
                                                  CANChannelNumericID channelID,
                                                  uint32_t minimumSamplingInterval,
                                                  uint32_t maxNumberOfSignalsToCollect,
                                                  uint32_t conditionId,
                                                  InspectionTimestamp &newestSignalTimestamp,
                                                  std::vector<CollectedCanRawFrame> &output )
{
    for ( auto &buf : mCanFrameBuffers )
    {
        if ( ( buf.mFrameID == canID ) && ( buf.mChannelID == channelID ) &&
             ( buf.mMinimumSampleIntervalMs == minimumSamplingInterval ) )
        {
            int pos = static_cast<int>( buf.mCurrentPosition );
            for ( uint32_t i = 0; i < std::min( maxNumberOfSignalsToCollect, buf.mCounter ); i++ )
            {
                // Ensure access is in bounds
                if ( pos < 0 )
                {
                    pos = static_cast<int>( buf.mSize ) - 1;
                }
                if ( pos >= static_cast<int>( buf.mSize ) )
                {
                    pos = 0;
                }
                auto &sample = buf.mBuffer[static_cast<uint32_t>( pos )];
                if ( ( !sample.isAlreadyConsumed( conditionId ) ) || ( !mSendDataOnlyOncePerCondition ) )
                {
                    output.emplace_back( canID, channelID, sample.mTimestamp, sample.mBuffer, sample.mSize );
                    sample.setAlreadyConsumed( conditionId, true );
                }
                newestSignalTimestamp = std::max( newestSignalTimestamp, sample.mTimestamp );
                pos--;
            }
            return;
        }
    }
}

std::shared_ptr<const TriggeredCollectionSchemeData>
CollectionInspectionEngine::collectData( ActiveCondition &condition,
                                         uint32_t conditionId,
                                         InspectionTimestamp &newestSignalTimestamp )
{
    std::shared_ptr<TriggeredCollectionSchemeData> collectedData = std::make_shared<TriggeredCollectionSchemeData>();
    collectedData->metaData = condition.mCondition.metaData;
    collectedData->triggerTime = condition.mLastTrigger.systemTimeMs;
    // Pack signals
    for ( auto &s : condition.mCondition.signals )
    {
        if ( !s.isConditionOnlySignal )
        {
            collectLastSignals( s.signalID,
                                s.minimumSampleIntervalMs,
                                s.sampleBufferSize,
                                conditionId,
                                newestSignalTimestamp,
                                collectedData->signals );
        }
    }

    // Pack raw frames
    for ( auto &c : condition.mCondition.canFrames )
    {
        collectLastCanFrames( c.frameID,
                              c.channelID,
                              c.minimumSampleIntervalMs,
                              c.sampleBufferSize,
                              conditionId,
                              newestSignalTimestamp,
                              collectedData->canFrames );
    }
    // Pack active DTCs if any
    if ( condition.mCondition.includeActiveDtcs &&
         ( ( !isActiveDTCsConsumed( conditionId ) ) || mSendDataOnlyOncePerCondition ) )
    {
        collectedData->mDTCInfo = mActiveDTCs;
        setActiveDTCsConsumed( conditionId, true );
    }
    // Pack geohash into data sender buffer if there's new geohash.
    // A new geohash is generated during geohash function node evaluation
    if ( mGeohashFunctionNode.hasNewGeohash() )
    {
        mGeohashFunctionNode.consumeGeohash( collectedData->mGeohashInfo );
    }
    // Propagate the event ID
    collectedData->eventID = condition.mEventID;
    return std::const_pointer_cast<const TriggeredCollectionSchemeData>( collectedData );
}

std::shared_ptr<const TriggeredCollectionSchemeData>
CollectionInspectionEngine::collectNextDataToSend( const TimePoint &currentTime, uint32_t &waitTimeMs )
{
    uint32_t minimumWaitTimeMs = std::numeric_limits<uint32_t>::max();
    if ( mConditionsNotTriggeredWaitingPublished.all() )
    {
        waitTimeMs = minimumWaitTimeMs;
        return std::shared_ptr<const TriggeredCollectionSchemeData>( nullptr );
    }
    for ( uint32_t i = 0; i < mConditions.size(); i++ )
    {
        if ( mNextConditionToCollectedIndex >= mConditions.size() )
        {
            mNextConditionToCollectedIndex = 0;
        }
        if ( !mConditionsNotTriggeredWaitingPublished.test( mNextConditionToCollectedIndex ) )
        {
            auto &condition = mConditions[mNextConditionToCollectedIndex];
            if ( ( ( condition.mLastTrigger.systemTimeMs == 0 ) && ( condition.mLastTrigger.monotonicTimeMs == 0 ) ) ||
                 ( currentTime.monotonicTimeMs >=
                   condition.mLastTrigger.monotonicTimeMs + condition.mCondition.afterDuration ) )
            {
                mConditionsNotTriggeredWaitingPublished.set( mNextConditionToCollectedIndex );
                // Send message out only with a certain probability. If probabilityToSend==0
                // no data is sent out
                if ( mDataReduction.shallSendData( condition.mCondition.probabilityToSend ) )
                {
                    // Generate the Event ID and pack  it into the active Condition
                    condition.mEventID = generateEventID( currentTime.systemTimeMs );
                    // Check if we need more data from other sensors
                    evaluateAndTriggerRichSensorCapture( condition );
                    // Return the collected data
                    InspectionTimestamp newestSignalTimeStamp = 0;
                    auto cd = collectData( condition, mNextConditionToCollectedIndex, newestSignalTimeStamp );
                    // After collecting the data set the newest timestamp from any data that was
                    // collected
                    condition.mLastDataTimestampPublished =
                        std::min( newestSignalTimeStamp, currentTime.monotonicTimeMs );
                    return cd;
                }
            }
            else
            {
                minimumWaitTimeMs =
                    std::min<uint32_t>( minimumWaitTimeMs,
                                        static_cast<uint32_t>( ( condition.mLastTrigger.monotonicTimeMs +
                                                                 condition.mCondition.afterDuration ) -
                                                               currentTime.monotonicTimeMs ) );
            }
        }
        mNextConditionToCollectedIndex++;
    }
    // No Data ready to be sent
    waitTimeMs = minimumWaitTimeMs;
    return std::shared_ptr<const TriggeredCollectionSchemeData>( nullptr );
}

void
CollectionInspectionEngine::evaluateAndTriggerRichSensorCapture( const ActiveCondition &condition )
{
    // Find out whether Image Data is needed.
    if ( condition.mCondition.includeImageCapture )
    {
        std::vector<EventMetadata> eventMetadata;
        // Create an Event Item for each device we want to get image from
        for ( const auto &imageCollectionInfo : condition.mCondition.imageCollectionInfos )
        {
            // Make sure the same Event ID is passed to the Image capture module
            // The event happened  afterDuration before now, and thus, the camera
            // data should have been requested already during the condition eval,
            // however, because the Camera request is completely not in scope of the
            // InspectionCycle, we want to make sure that no data leaves the system
            // if it does not fit into the probability. We also want to make sure
            // we don 't trigger camera collection if the probably meanwhile changed.
            // That's why the PositiveOffset is set to zero.
            eventMetadata.emplace_back( condition.mEventID,
                                        imageCollectionInfo.deviceID,
                                        imageCollectionInfo.beforeDurationMs + condition.mCondition.afterDuration,
                                        0 );
        }
        // This is non blocking. Listeners simply copy the metadata.
        notifyListeners<const std::vector<EventMetadata> &>( &InspectionEventListener::onEventOfInterestDetected,
                                                             eventMetadata );
    }
}

void
CollectionInspectionEngine::addNewSignal( InspectionSignalID id, const TimePoint &receiveTime, InspectionValue value )
{
    if ( mSignalBuffers.find( id ) == mSignalBuffers.end() || mSignalBuffers[id].empty() )
    {
        // Signal not collected by any active condition
        return;
    }
    // Iterate through all sampling intervals of the signal
    for ( auto &buf : mSignalBuffers[id] )
    {
        if ( ( buf.mSize > 0 ) && ( buf.mSize <= buf.mBuffer.size() ) &&
             ( ( buf.mMinimumSampleIntervalMs == 0 ) ||
               ( ( buf.mLastSample.systemTimeMs == 0 ) && ( buf.mLastSample.monotonicTimeMs == 0 ) ) ||
               ( receiveTime.monotonicTimeMs >= buf.mLastSample.monotonicTimeMs + buf.mMinimumSampleIntervalMs ) ) )
        {
            buf.mCurrentPosition++;
            if ( buf.mCurrentPosition >= buf.mSize )
            {
                buf.mCurrentPosition = 0;
            }
            buf.mBuffer[buf.mCurrentPosition].mValue = value;
            buf.mBuffer[buf.mCurrentPosition].mTimestamp = receiveTime.systemTimeMs;
            buf.mBuffer[buf.mCurrentPosition].setAlreadyConsumed( ALL_CONDITIONS, false );
            buf.mCounter++;
            buf.mLastSample = receiveTime;
            for ( auto &window : buf.mWindowFunctionData )
            {
                window.addValue( value, receiveTime.monotonicTimeMs, mNextWindowFunctionTimesOut );
            }
            mConditionsWithInputSignalChanged |= buf.mConditionsThatEvaluateOnThisSignal;
        }
    }
}

void
CollectionInspectionEngine::addNewRawCanFrame( CANRawFrameID canID,
                                               CANChannelNumericID channelID,
                                               const TimePoint &receiveTime,
                                               std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> &buffer,
                                               uint8_t size )
{
    for ( auto &buf : mCanFrameBuffers )
    {
        if ( ( buf.mFrameID == canID ) && ( buf.mChannelID == channelID ) )
        {
            if ( ( buf.mSize > 0 ) && ( buf.mSize <= buf.mBuffer.size() ) &&
                 ( ( buf.mMinimumSampleIntervalMs == 0 ) ||
                   ( ( buf.mLastSample.systemTimeMs == 0 ) && ( buf.mLastSample.monotonicTimeMs == 0 ) ) ||
                   ( receiveTime.monotonicTimeMs >= buf.mLastSample.monotonicTimeMs + buf.mMinimumSampleIntervalMs ) ) )
            {
                buf.mCurrentPosition++;
                if ( buf.mCurrentPosition >= buf.mSize )
                {
                    buf.mCurrentPosition = 0;
                }
                buf.mBuffer[buf.mCurrentPosition].mSize =
                    std::min( size, static_cast<uint8_t>( buf.mBuffer[buf.mCurrentPosition].mBuffer.size() ) );
                for ( size_t i = 0; i < buf.mBuffer[buf.mCurrentPosition].mSize; i++ )
                {
                    buf.mBuffer[buf.mCurrentPosition].mBuffer[i] = buffer[i];
                }
                buf.mBuffer[buf.mCurrentPosition].mTimestamp = receiveTime.systemTimeMs;
                buf.mBuffer[buf.mCurrentPosition].setAlreadyConsumed( ALL_CONDITIONS, false );
                buf.mCounter++;
                buf.mLastSample = receiveTime;
            }
        }
    }
}

void
CollectionInspectionEngine::setActiveDTCs( const DTCInfo &activeDTCs )
{
    setActiveDTCsConsumed( ALL_CONDITIONS, false );
    mActiveDTCs = activeDTCs;
}

CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getLatestSignalValue( InspectionSignalID id,
                                                  ActiveCondition &condition,
                                                  InspectionValue &result )
{
    auto mapLookup = condition.mEvaluationSignals.find( id );
    if ( ( mapLookup == condition.mEvaluationSignals.end() ) || ( mapLookup->second == nullptr ) )
    {
        mLogger.warn( "CollectionInspectionEngine::getLatestSignalValue", "SIGNAL_NOT_FOUND" );
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    SignalHistoryBuffer *s = mapLookup->second;
    if ( s->mCounter == 0 )
    {
        // Not a single sample collected yet
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    result = s->mBuffer[s->mCurrentPosition].mValue;
    return ExpressionErrorCode::SUCCESSFUL;
}

CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getSampleWindowFunction( WindowFunction function,
                                                     InspectionSignalID signalID,
                                                     ActiveCondition &condition,
                                                     InspectionValue &result )
{
    auto mapLookup = condition.mEvaluationFunctions.find( signalID );
    if ( ( mapLookup == condition.mEvaluationFunctions.end() ) || ( mapLookup->second == nullptr ) )
    {
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    auto w = mapLookup->second;

    switch ( function )
    {
    case WindowFunction::LAST_FIXED_WINDOW_AVG:
        result = w->mLastAvg;
        return w->mLastAvailable ? ExpressionErrorCode::SUCCESSFUL : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::LAST_FIXED_WINDOW_MIN:
        result = w->mLastMin;
        return w->mLastAvailable ? ExpressionErrorCode::SUCCESSFUL : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::LAST_FIXED_WINDOW_MAX:
        result = w->mLastMax;
        return w->mLastAvailable ? ExpressionErrorCode::SUCCESSFUL : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::PREV_LAST_FIXED_WINDOW_AVG:
        result = w->mPreviousLastAvg;
        return w->mPreviousLastAvailable ? ExpressionErrorCode::SUCCESSFUL
                                         : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::PREV_LAST_FIXED_WINDOW_MIN:
        result = w->mPreviousLastMin;
        return w->mPreviousLastAvailable ? ExpressionErrorCode::SUCCESSFUL
                                         : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::PREV_LAST_FIXED_WINDOW_MAX:
        result = w->mPreviousLastMax;
        return w->mPreviousLastAvailable ? ExpressionErrorCode::SUCCESSFUL
                                         : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    default:
        return ExpressionErrorCode::NOT_IMPLEMENTED_FUNCTION;
    }
}

CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getGeohashFunctionNode( const struct ExpressionNode *expression,
                                                    ActiveCondition &condition,
                                                    bool &resultValueBool )
{
    resultValueBool = false;
    // First we need to grab Latitude / longitude signal from collected signal buffer
    InspectionValue latitude = 0;
    auto status = getLatestSignalValue( expression->function.geohashFunction.latitudeSignalID, condition, latitude );
    if ( status != ExpressionErrorCode::SUCCESSFUL )
    {
        mLogger.warn( "CollectionInspectionEngine::getGeohashFunctionNode",
                      "Unable to evaluate Geohash due to missing latitude signal" );
        return status;
    }
    InspectionValue longitude = 0;
    status = getLatestSignalValue( expression->function.geohashFunction.longitudeSignalID, condition, longitude );
    if ( status != ExpressionErrorCode::SUCCESSFUL )
    {
        mLogger.warn( "CollectionInspectionEngine::getGeohashFunctionNode",
                      "Unable to evaluate Geohash due to missing longitude signal" );
        return status;
    }
    resultValueBool = mGeohashFunctionNode.evaluateGeohash( latitude,
                                                            longitude,
                                                            expression->function.geohashFunction.precision,
                                                            expression->function.geohashFunction.gpsUnitType );
    return ExpressionErrorCode::SUCCESSFUL;
}

CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::eval( const struct ExpressionNode *expression,
                                  ActiveCondition &condition,
                                  InspectionValue &resultValueDouble,
                                  bool &resultValueBool,
                                  int remainingStackDepth )
{
    if ( ( remainingStackDepth <= 0 ) || ( expression == nullptr ) )
    {
        mLogger.warn( "CollectionInspectionEngine::eval", "STACK_DEPTH_REACHED or nullptr" );
        return ExpressionErrorCode::STACK_DEPTH_REACHED;
    }
    if ( expression->nodeType == ExpressionNodeType::FLOAT )
    {
        resultValueDouble = expression->floatingValue;
        return ExpressionErrorCode::SUCCESSFUL;
    }
    if ( expression->nodeType == ExpressionNodeType::BOOLEAN )
    {
        resultValueBool = expression->booleanValue;
        return ExpressionErrorCode::SUCCESSFUL;
    }
    if ( expression->nodeType == ExpressionNodeType::SIGNAL )
    {
        return getLatestSignalValue( expression->signalID, condition, resultValueDouble );
    }
    if ( expression->nodeType == ExpressionNodeType::WINDOWFUNCTION )
    {
        return getSampleWindowFunction(
            expression->function.windowFunction, expression->signalID, condition, resultValueDouble );
    }
    if ( expression->nodeType == ExpressionNodeType::GEOHASHFUNCTION )
    {
        return getGeohashFunctionNode( expression, condition, resultValueBool );
    }

    InspectionValue leftDouble = 0;
    InspectionValue rightDouble = 0;
    bool leftBool = false;
    bool rightBool = false;
    ExpressionErrorCode leftRet = ExpressionErrorCode::SUCCESSFUL;
    ExpressionErrorCode rightRet = ExpressionErrorCode::SUCCESSFUL;
    // Recursion limited depth through last parameter
    leftRet = eval( expression->left, condition, leftDouble, leftBool, remainingStackDepth - 1 );

    if ( leftRet != ExpressionErrorCode::SUCCESSFUL )
    {
        return leftRet;
    }

    // Logical NOT operator does not have a right operand, hence expression->right can be nullptr
    if ( expression->nodeType != ExpressionNodeType::OPERATOR_LOGICAL_NOT )
    {
        // No short-circuit evaluation so always evaluate right part
        rightRet = eval( expression->right, condition, rightDouble, rightBool, remainingStackDepth - 1 );

        if ( rightRet != ExpressionErrorCode::SUCCESSFUL )
        {
            return rightRet;
        }
    }

    switch ( expression->nodeType )
    {
    case ExpressionNodeType::OPERATOR_SMALLER:
        resultValueBool = leftDouble < rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_BIGGER:
        resultValueBool = leftDouble > rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_SMALLER_EQUAL:
        resultValueBool = leftDouble <= rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_BIGGER_EQUAL:
        resultValueBool = leftDouble >= rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_EQUAL:
        resultValueBool = std::abs( leftDouble - rightDouble ) < EVAL_EQUAL_DISTANCE();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_NOT_EQUAL:
        resultValueBool = !( std::abs( leftDouble - rightDouble ) < EVAL_EQUAL_DISTANCE() );
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_LOGICAL_AND:
        resultValueBool = leftBool && rightBool;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_LOGICAL_OR:
        resultValueBool = leftBool || rightBool;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_LOGICAL_NOT:
        resultValueBool = !leftBool;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS:
        resultValueDouble = leftDouble + rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS:
        resultValueDouble = leftDouble - rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_MULTIPLY:
        resultValueDouble = leftDouble * rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_DIVIDE:
        resultValueDouble = leftDouble / rightDouble;
        return ExpressionErrorCode::SUCCESSFUL;
    default:
        return ExpressionErrorCode::NOT_IMPLEMENTED_TYPE;
    }
}

bool
CollectionInspectionEngine::FixedTimeWindowFunctionData::updateWindow( InspectionTimestamp timestamp,
                                                                       InspectionTimestamp &nextWindowFunctionTimesOut )
{
    if ( mLastTimeCalculated == 0 )
    {
        // First time a signal arrives start the window for this signal
        mLastTimeCalculated = timestamp;
        initNewWindow( timestamp, nextWindowFunctionTimesOut );
    }
    // check the last 2 windows as this class records the last and previous last data
    else if ( timestamp >= mLastTimeCalculated + mWindowSizeMs * 2 )
    {
        // In the last window not a single sample arrived
        mLastAvailable = false;
        if ( mCollectedSignals == 0 )
        {
            mPreviousLastAvailable = false;
        }
        else
        {
            mPreviousLastAvailable = true;
            mPreviousLastMin = mCollectingMin;
            mPreviousLastMax = mCollectingMax;
            mPreviousLastAvg = mCollectingSum / mCollectedSignals;
        }
        initNewWindow( timestamp, nextWindowFunctionTimesOut );
    }
    else if ( timestamp >= mLastTimeCalculated + mWindowSizeMs )
    {
        mPreviousLastMin = mLastMin;
        mPreviousLastMax = mLastMax;
        mPreviousLastAvg = mLastAvg;
        mPreviousLastAvailable = mLastAvailable;
        if ( mCollectedSignals == 0 )
        {
            mLastAvailable = false;
        }
        else
        {
            mLastAvailable = true;
            mLastMin = mCollectingMin;
            mLastMax = mCollectingMax;
            mLastAvg = mCollectingSum / mCollectedSignals;
        }
        initNewWindow( timestamp, nextWindowFunctionTimesOut );
    }
    else
    {
        nextWindowFunctionTimesOut = std::min( nextWindowFunctionTimesOut, mLastTimeCalculated + mWindowSizeMs );
        return false;
    }
    return true;
}

EventID
CollectionInspectionEngine::generateEventID( InspectionTimestamp timestamp )
{
    // Generate an eventId as a combination of an event counter and a timestamp
    uint32_t eventId = static_cast<uint32_t>( ( generateEventCounter() & 0xFF ) | ( timestamp << 8 ) );
    // As Kotlin reads eventId as int32, set most significant bit to 0 so event IDs stay positive
    eventId = eventId & 0x7FFFFFFF;
    return eventId;
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
