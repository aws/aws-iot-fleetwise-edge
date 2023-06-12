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

template <typename T>
void
CollectionInspectionEngine::addSignalToBuffer( const InspectionMatrixSignalCollectionInfo &signalIn )
{
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    const auto signalIDIn = signalIn.signalID;

    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( signalIDIn );

    if ( signalHistoryBufferPtr == nullptr )
    {
        return;
    }
    auto &bufferVec = *signalHistoryBufferPtr;
    for ( auto &buffer : bufferVec )
    {
        if ( buffer.mMinimumSampleIntervalMs == signalIn.minimumSampleIntervalMs )
        {
            buffer.mSize = std::max( buffer.mSize, signalIn.sampleBufferSize );
            buffer.addFixedWindow( signalIn.fixedWindowPeriod );
            return;
        }
    }
    bufferVec.emplace_back( signalIn.sampleBufferSize, signalIn.minimumSampleIntervalMs );
    bufferVec.back().addFixedWindow( signalIn.fixedWindowPeriod );
}

void
CollectionInspectionEngine::onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
{
    // Clears everything in this class including all data in the signal history buffer
    clear();
    mActiveInspectionMatrix = inspectionMatrix; // Pointers and references into this memory are maintained so hold
                                                // a shared_ptr to it so it does not get deleted
    mConditionsNotTriggeredWaitingPublished.set();
    for ( auto &p : mActiveInspectionMatrix->conditions )
    {
        // Check if we can add an additional condition to mConditions
        if ( mConditions.size() >= MAX_NUMBER_OF_ACTIVE_CONDITION )
        {
            TraceModule::get().incrementVariable( TraceVariable::CE_TOO_MANY_CONDITIONS );
            FWE_LOG_WARN( "Too many conditions are active. Up to " +
                          std::to_string( MAX_NUMBER_OF_ACTIVE_CONDITION - 1 ) +
                          " conditions can be active at a time. Additional conditions will be skipped" );
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
            break;
        }
        mConditions.emplace_back( p );
        if ( p.signals.size() > MAX_DIFFERENT_SIGNAL_IDS )
        {
            TraceModule::get().incrementVariable( TraceVariable::CE_SIGNAL_ID_OUTBOUND );
            FWE_LOG_ERROR( "There can be only " + std::to_string( MAX_DIFFERENT_SIGNAL_IDS ) +
                           " different signal IDs" );
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
            return;
        }
        for ( auto &s : p.signals )
        {
            if ( s.signalID == INVALID_SIGNAL_ID )
            {
                FWE_LOG_ERROR( "A SignalID with value" + std::to_string( INVALID_SIGNAL_ID ) + " is not allowed" );
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
                return;
            }
            if ( s.sampleBufferSize == 0 )
            {
                TraceModule::get().incrementVariable( TraceVariable::CE_SAMPLE_SIZE_ZERO );
                FWE_LOG_ERROR( "A Sample buffer size of 0 is not allowed" );
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
                return;
            }
            auto signalIDIn = s.signalID;
            mSignalToBufferTypeMap.insert( { signalIDIn, s.signalType } );
            switch ( s.signalType )
            {
            case SignalType::UINT8:
                addSignalToBuffer<uint8_t>( s );
                break;
            case SignalType::INT8:
                addSignalToBuffer<int8_t>( s );
                break;
            case SignalType::UINT16:
                addSignalToBuffer<uint16_t>( s );
                break;
            case SignalType::INT16:
                addSignalToBuffer<int16_t>( s );
                break;
            case SignalType::UINT32:
                addSignalToBuffer<uint32_t>( s );
                break;
            case SignalType::INT32:
                addSignalToBuffer<int32_t>( s );
                break;
            case SignalType::UINT64:
                addSignalToBuffer<uint64_t>( s );
                break;
            case SignalType::INT64:
                addSignalToBuffer<int64_t>( s );
                break;
            case SignalType::FLOAT:
                addSignalToBuffer<float>( s );
                break;
            case SignalType::DOUBLE:
                addSignalToBuffer<double>( s );
                break;
            case SignalType::BOOLEAN:
                addSignalToBuffer<bool>( s );
                break;
            default:
                break;
            }
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
            switch ( s.signalType )
            {
            case SignalType::UINT8:
                updateConditionBuffer<uint8_t>( s, ac, conditionIndex );
                break;
            case SignalType::INT8:
                updateConditionBuffer<int8_t>( s, ac, conditionIndex );
                break;
            case SignalType::UINT16:
                updateConditionBuffer<uint16_t>( s, ac, conditionIndex );
                break;
            case SignalType::INT16:
                updateConditionBuffer<int16_t>( s, ac, conditionIndex );
                break;
            case SignalType::UINT32:
                updateConditionBuffer<uint32_t>( s, ac, conditionIndex );
                break;
            case SignalType::INT32:
                updateConditionBuffer<int32_t>( s, ac, conditionIndex );
                break;
            case SignalType::UINT64:
                updateConditionBuffer<uint64_t>( s, ac, conditionIndex );
                break;
            case SignalType::INT64:
                updateConditionBuffer<int64_t>( s, ac, conditionIndex );
                break;
            case SignalType::FLOAT:
                updateConditionBuffer<float>( s, ac, conditionIndex );
                break;
            case SignalType::DOUBLE:
                updateConditionBuffer<double>( s, ac, conditionIndex );
                break;
            case SignalType::BOOLEAN:
                updateConditionBuffer<bool>( s, ac, conditionIndex );
                break;
            default:
                break;
            }
        }
    }

    // Assume all conditions are currently true;
    mConditionsWithConditionCurrentlyTrue.set();

    (void)preAllocateBuffers();
}

template <typename T>
void
CollectionInspectionEngine::updateConditionBuffer(
    const InspectionMatrixSignalCollectionInfo &inspectionMatrixCollectionInfoIn,
    ActiveCondition &acIn,
    const long unsigned int conditionIndexIn )
{
    SignalID signalIDIn = inspectionMatrixCollectionInfoIn.signalID;
    SignalHistoryBuffer<T> *buf = nullptr;
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;

    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( signalIDIn );

    if ( signalHistoryBufferPtr != nullptr )
    {
        auto &bufferVec = *signalHistoryBufferPtr;
        for ( auto &buffer : bufferVec )
        {
            if ( buffer.mMinimumSampleIntervalMs == inspectionMatrixCollectionInfoIn.minimumSampleIntervalMs )
            {
                buf = &buffer;
                break;
            }
        }
    }
    if ( ( buf != nullptr ) && isSignalPartOfEval( acIn.mCondition.condition, signalIDIn, MAX_EQUATION_DEPTH ) )
    {
        buf->mConditionsThatEvaluateOnThisSignal.set( conditionIndexIn );
        // acIn.mEvaluationSignals[signalIDIn] = buf;
        acIn.mEvaluationSignals.insert( { signalIDIn, buf } );
        FixedTimeWindowFunctionData<T> *window =
            buf->getFixedWindow( inspectionMatrixCollectionInfoIn.fixedWindowPeriod );
        if ( window != nullptr )
        {
            // acIn.mEvaluationFunctions[signalIDIn] = window;
            acIn.mEvaluationFunctions.insert( { signalIDIn, window } );
        }
    }
}

template <typename T>
bool
CollectionInspectionEngine::allocateBufferVector( SignalID signalIDIn, uint32_t &usedBytes )
{
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( signalIDIn );

    if ( signalHistoryBufferPtr != nullptr )
    {
        auto &bufferVec = *signalHistoryBufferPtr;
        for ( auto &signal : bufferVec )
        {
            uint64_t requiredBytes = signal.mSize * static_cast<uint64_t>( sizeof( struct SignalSample<T> ) );
            if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
            {
                FWE_LOG_WARN( "The requested " + std::to_string( signal.mSize ) +
                              " number of signal samples leads to a memory requirement  that's above the maximum "
                              "configured of " +
                              std::to_string( MAX_SAMPLE_MEMORY ) + "Bytes" );
                signal.mSize = 0;
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
                return false;
            }
            usedBytes += static_cast<uint32_t>( requiredBytes );

            // reserve the size like new[]
            signal.mBuffer.resize( signal.mSize );
        }
    }
    return true;
}

bool
CollectionInspectionEngine::preAllocateBuffers()
{
    // Allocate size
    uint32_t usedBytes = 0;

    // Allocate Signal Buffer
    for ( auto &bufferVector : mSignalBuffers )
    {
        auto signalID = bufferVector.first;
        if ( mSignalToBufferTypeMap.find( signalID ) != mSignalToBufferTypeMap.end() )
        {
            auto signalType = mSignalToBufferTypeMap[signalID];
            switch ( signalType )
            {
            case SignalType::UINT8:
                if ( !allocateBufferVector<uint8_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::INT8:
                if ( !allocateBufferVector<int8_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::UINT16:
                if ( !allocateBufferVector<uint16_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::INT16:
                if ( !allocateBufferVector<int16_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::UINT32:
                if ( !allocateBufferVector<uint32_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::INT32:
                if ( !allocateBufferVector<int32_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::UINT64:
                if ( !allocateBufferVector<uint64_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::INT64:
                if ( !allocateBufferVector<int64_t>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::FLOAT:
                if ( !allocateBufferVector<float>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::DOUBLE:
                if ( !allocateBufferVector<double>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            case SignalType::BOOLEAN:
                if ( !allocateBufferVector<bool>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            default:
                if ( !allocateBufferVector<double>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
            }
        }
    }
    // Allocate Can buffer
    for ( auto &buf : mCanFrameBuffers )
    {
        uint64_t requiredBytes = buf.mSize * static_cast<uint64_t>( sizeof( struct CanFrameSample ) );
        if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
        {
            FWE_LOG_WARN( "The requested " + std::to_string( buf.mSize ) +
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
    TraceModule::get().setVariable( TraceVariable::SIGNAL_BUFFER_SIZE, usedBytes );
    return true;
}

void
CollectionInspectionEngine::clear()
{
    mSignalBuffers.clear();
    mSignalToBufferTypeMap.clear();
    mCanFrameBuffers.clear();
    mConditions.clear();
    mNextConditionToCollectedIndex = 0;
    mNextWindowFunctionTimesOut = 0;
    mConditionsWithInputSignalChanged.reset();
    mConditionsWithConditionCurrentlyTrue.reset();
    mConditionsNotTriggeredWaitingPublished.reset();
}

template <typename T>
void
CollectionInspectionEngine::updateBufferFixedWindowFunction( SignalID signalIDIn, InspectionTimestamp timestamp )
{
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    try
    {
        if ( mSignalBuffers.find( signalIDIn ) != mSignalBuffers.end() )
        {
            auto &mapVal = mSignalBuffers.at( signalIDIn );
            signalHistoryBufferPtr = boost::get<std::vector<SignalHistoryBuffer<T>>>( &mapVal );
        }
    }
    catch ( ... )
    {
        FWE_LOG_ERROR( "Failed to retrieve signalHistoryBuffer vector for signal ID " + std::to_string( signalIDIn ) );
        return;
    }
    if ( signalHistoryBufferPtr != nullptr )
    {
        auto &bufferVec = *signalHistoryBufferPtr;
        for ( auto &signal : bufferVec )
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

void
CollectionInspectionEngine::updateAllFixedWindowFunctions( InspectionTimestamp timestamp )
{
    mNextWindowFunctionTimesOut = std::numeric_limits<InspectionTimestamp>::max();
    for ( auto &signalVector : mSignalBuffers )
    {
        auto signalID = signalVector.first;
        if ( mSignalToBufferTypeMap.find( signalID ) != mSignalToBufferTypeMap.end() )
        {

            auto signalType = mSignalToBufferTypeMap[signalID];
            switch ( signalType )
            {
            case SignalType::UINT8:
                updateBufferFixedWindowFunction<uint8_t>( signalID, timestamp );
                break;
            case SignalType::INT8:
                updateBufferFixedWindowFunction<int8_t>( signalID, timestamp );
                break;
            case SignalType::UINT16:
                updateBufferFixedWindowFunction<uint16_t>( signalID, timestamp );
                break;
            case SignalType::INT16:
                updateBufferFixedWindowFunction<int16_t>( signalID, timestamp );
                break;
            case SignalType::UINT32:
                updateBufferFixedWindowFunction<uint32_t>( signalID, timestamp );
                break;
            case SignalType::INT32:
                updateBufferFixedWindowFunction<int32_t>( signalID, timestamp );
                break;
            case SignalType::UINT64:

                updateBufferFixedWindowFunction<uint64_t>( signalID, timestamp );
                break;
            case SignalType::INT64:
                updateBufferFixedWindowFunction<int64_t>( signalID, timestamp );
                break;
            case SignalType::FLOAT:
                updateBufferFixedWindowFunction<float>( signalID, timestamp );
                break;
            case SignalType::DOUBLE:
                updateBufferFixedWindowFunction<double>( signalID, timestamp );
                break;
            case SignalType::BOOLEAN:
                updateBufferFixedWindowFunction<bool>( signalID, timestamp );
                break;
            default:
                break;
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

template <typename T = double>
void
CollectionInspectionEngine::collectLastSignals( InspectionSignalID id,
                                                uint32_t minimumSamplingInterval,
                                                uint32_t maxNumberOfSignalsToCollect,
                                                uint32_t conditionId,
                                                SignalType signalTypeIn,
                                                InspectionTimestamp &newestSignalTimestamp,
                                                std::vector<CollectedSignal> &output )
{
    if ( mSignalBuffers[id].empty() )
    {
        // Signal not collected by any active condition
        return;
    }
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( id );
    if ( signalHistoryBufferPtr == nullptr )
    {
        // Access by Invalid DataType
        return;
    }
    auto &bufferVec = *signalHistoryBufferPtr;
    for ( auto &buf : bufferVec )
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
                    output.emplace_back( id, sample.mTimestamp, sample.mValue, signalTypeIn );
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
            switch ( s.signalType )
            {
            case SignalType::UINT8:
                collectLastSignals<uint8_t>( s.signalID,
                                             s.minimumSampleIntervalMs,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             collectedData->signals );
                break;
            case SignalType::INT8:
                collectLastSignals<int8_t>( s.signalID,
                                            s.minimumSampleIntervalMs,
                                            s.sampleBufferSize,
                                            conditionId,
                                            s.signalType,
                                            newestSignalTimestamp,
                                            collectedData->signals );
                break;
            case SignalType::UINT16:
                collectLastSignals<uint16_t>( s.signalID,
                                              s.minimumSampleIntervalMs,
                                              s.sampleBufferSize,
                                              conditionId,
                                              s.signalType,
                                              newestSignalTimestamp,
                                              collectedData->signals );
                break;
            case SignalType::INT16:
                collectLastSignals<int16_t>( s.signalID,
                                             s.minimumSampleIntervalMs,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             collectedData->signals );
                break;
            case SignalType::UINT32:
                collectLastSignals<uint32_t>( s.signalID,
                                              s.minimumSampleIntervalMs,
                                              s.sampleBufferSize,
                                              conditionId,
                                              s.signalType,
                                              newestSignalTimestamp,
                                              collectedData->signals );
                break;
            case SignalType::INT32:
                collectLastSignals<int32_t>( s.signalID,
                                             s.minimumSampleIntervalMs,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             collectedData->signals );
                break;
            case SignalType::UINT64:
                collectLastSignals<uint64_t>( s.signalID,
                                              s.minimumSampleIntervalMs,
                                              s.sampleBufferSize,
                                              conditionId,
                                              s.signalType,
                                              newestSignalTimestamp,
                                              collectedData->signals );
                break;
            case SignalType::INT64:
                collectLastSignals<int64_t>( s.signalID,
                                             s.minimumSampleIntervalMs,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             collectedData->signals );
                break;
            case SignalType::FLOAT:
                collectLastSignals<float>( s.signalID,
                                           s.minimumSampleIntervalMs,
                                           s.sampleBufferSize,
                                           conditionId,
                                           s.signalType,
                                           newestSignalTimestamp,
                                           collectedData->signals );
                break;
            case SignalType::DOUBLE:
                collectLastSignals<double>( s.signalID,
                                            s.minimumSampleIntervalMs,
                                            s.sampleBufferSize,
                                            conditionId,
                                            s.signalType,
                                            newestSignalTimestamp,
                                            collectedData->signals );
                break;
            case SignalType::BOOLEAN:
                collectLastSignals<bool>( s.signalID,
                                          s.minimumSampleIntervalMs,
                                          s.sampleBufferSize,
                                          conditionId,
                                          s.signalType,
                                          newestSignalTimestamp,
                                          collectedData->signals );
                break;
            default:
                break;
            }
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

template <typename T>
CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getLatestBufferSignalValue( InspectionSignalID id,
                                                        ActiveCondition &condition,
                                                        InspectionValue &result )
{

    auto *s = condition.getEvaluationSignalsBufferPtr<T>( id );
    if ( s == nullptr )
    {
        FWE_LOG_WARN( "SIGNAL_NOT_FOUND" );
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    if ( s->mCounter == 0 )
    {
        // Not a single sample collected yet
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    result = static_cast<double>( s->mBuffer[s->mCurrentPosition].mValue );
    return ExpressionErrorCode::SUCCESSFUL;
}

CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getLatestSignalValue( InspectionSignalID id,
                                                  ActiveCondition &condition,
                                                  InspectionValue &result )
{
    if ( mSignalToBufferTypeMap.find( id ) == mSignalToBufferTypeMap.end() )
    {
        FWE_LOG_WARN( "SIGNAL_NOT_FOUND" );
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    auto signalType = mSignalToBufferTypeMap[id];
    switch ( signalType )
    {
    case SignalType::UINT8:
        return getLatestBufferSignalValue<uint8_t>( id, condition, result );
        break;
    case SignalType::INT8:
        return getLatestBufferSignalValue<int8_t>( id, condition, result );
        break;
    case SignalType::UINT16:
        return getLatestBufferSignalValue<uint16_t>( id, condition, result );
        break;
    case SignalType::INT16:
        return getLatestBufferSignalValue<uint16_t>( id, condition, result );
        break;
    case SignalType::UINT32:
        return getLatestBufferSignalValue<uint32_t>( id, condition, result );
        break;
    case SignalType::INT32:
        return getLatestBufferSignalValue<int32_t>( id, condition, result );
        break;
    case SignalType::UINT64:
        return getLatestBufferSignalValue<uint64_t>( id, condition, result );
        break;
    case SignalType::INT64:
        return getLatestBufferSignalValue<int64_t>( id, condition, result );
        break;
    case SignalType::FLOAT:
        return getLatestBufferSignalValue<float>( id, condition, result );
        break;
    case SignalType::DOUBLE:
        return getLatestBufferSignalValue<double>( id, condition, result );
        break;
    case SignalType::BOOLEAN:
        return getLatestBufferSignalValue<bool>( id, condition, result );
        break;
    default:
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
        break;
    }
}

template <typename T = double>
CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getSampleWindowFunctionType( WindowFunction function,
                                                         InspectionSignalID signalID,
                                                         ActiveCondition &condition,
                                                         InspectionValue &result )
{
    auto w = condition.getFixedTimeWindowFunctionDataPtr<T>( signalID );
    if ( w == nullptr )
    {
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }

    switch ( function )
    {
    case WindowFunction::LAST_FIXED_WINDOW_AVG:
        result = static_cast<double>( w->mLastAvg );
        return w->mLastAvailable ? ExpressionErrorCode::SUCCESSFUL : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::LAST_FIXED_WINDOW_MIN:
        result = static_cast<double>( w->mLastMin );
        return w->mLastAvailable ? ExpressionErrorCode::SUCCESSFUL : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::LAST_FIXED_WINDOW_MAX:
        result = static_cast<double>( w->mLastMax );
        return w->mLastAvailable ? ExpressionErrorCode::SUCCESSFUL : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::PREV_LAST_FIXED_WINDOW_AVG:
        result = static_cast<double>( w->mPreviousLastAvg );
        return w->mPreviousLastAvailable ? ExpressionErrorCode::SUCCESSFUL
                                         : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::PREV_LAST_FIXED_WINDOW_MIN:
        result = static_cast<double>( w->mPreviousLastMin );
        return w->mPreviousLastAvailable ? ExpressionErrorCode::SUCCESSFUL
                                         : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    case WindowFunction::PREV_LAST_FIXED_WINDOW_MAX:
        result = static_cast<double>( w->mPreviousLastMax );
        return w->mPreviousLastAvailable ? ExpressionErrorCode::SUCCESSFUL
                                         : ExpressionErrorCode::FUNCTION_DATA_NOT_AVAILABLE;
    default:
        return ExpressionErrorCode::NOT_IMPLEMENTED_FUNCTION;
    }
}

CollectionInspectionEngine::ExpressionErrorCode
CollectionInspectionEngine::getSampleWindowFunction( WindowFunction function,
                                                     InspectionSignalID signalID,
                                                     ActiveCondition &condition,
                                                     InspectionValue &result )
{
    if ( mSignalToBufferTypeMap.find( signalID ) == mSignalToBufferTypeMap.end() )
    {
        FWE_LOG_WARN( "SIGNAL_NOT_FOUND" );
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    auto signalType = mSignalToBufferTypeMap[signalID];
    switch ( signalType )
    {
    case SignalType::UINT8:
        return getSampleWindowFunctionType<uint8_t>( function, signalID, condition, result );
        break;
    case SignalType::INT8:
        return getSampleWindowFunctionType<int8_t>( function, signalID, condition, result );
        break;
    case SignalType::UINT16:
        return getSampleWindowFunctionType<uint16_t>( function, signalID, condition, result );
        break;
    case SignalType::INT16:
        return getSampleWindowFunctionType<int16_t>( function, signalID, condition, result );
        break;
    case SignalType::UINT32:
        return getSampleWindowFunctionType<uint32_t>( function, signalID, condition, result );
        break;
    case SignalType::INT32:
        return getSampleWindowFunctionType<int32_t>( function, signalID, condition, result );
        break;
    case SignalType::UINT64:
        return getSampleWindowFunctionType<uint64_t>( function, signalID, condition, result );
        break;
    case SignalType::INT64:
        return getSampleWindowFunctionType<int64_t>( function, signalID, condition, result );
        break;
    case SignalType::FLOAT:
        return getSampleWindowFunctionType<float>( function, signalID, condition, result );
        break;
    case SignalType::DOUBLE:
        return getSampleWindowFunctionType<double>( function, signalID, condition, result );
        break;
    case SignalType::BOOLEAN:
        return getSampleWindowFunctionType<bool>( function, signalID, condition, result );
        break;
    default:
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
        break;
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
        FWE_LOG_WARN( "Unable to evaluate Geohash due to missing latitude signal" );
        return status;
    }
    InspectionValue longitude = 0;
    status = getLatestSignalValue( expression->function.geohashFunction.longitudeSignalID, condition, longitude );
    if ( status != ExpressionErrorCode::SUCCESSFUL )
    {
        FWE_LOG_WARN( "Unable to evaluate Geohash due to missing longitude signal" );
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
        FWE_LOG_WARN( "STACK_DEPTH_REACHED or nullptr" );
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

EventID
CollectionInspectionEngine::generateEventID( InspectionTimestamp timestamp )
{
    // Generate an eventId as a combination of an event counter and a timestamp
    uint32_t eventId = static_cast<uint32_t>( generateEventCounter() ) | static_cast<uint32_t>( timestamp << 8 );
    // As Kotlin reads eventId as int32, set most significant bit to 0 so event IDs stay positive
    eventId = eventId & 0x7FFFFFFF;
    return eventId;
}

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
