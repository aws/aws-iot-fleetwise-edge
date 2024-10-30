// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionInspectionEngine.h"
#include "TraceModule.h"
#include <algorithm>
#include <cstdlib>

namespace Aws
{
namespace IoTFleetWise
{

CollectionInspectionEngine::CollectionInspectionEngine( bool sendDataOnlyOncePerCondition )
    : mSendDataOnlyOncePerCondition( sendDataOnlyOncePerCondition )
{
    setActiveDTCsConsumed( ALL_CONDITIONS, false );
}

template <typename T>
void
CollectionInspectionEngine::addSignalBuffer( const InspectionMatrixSignalCollectionInfo &signal )
{
    auto signalHistoryBufferVectorPtr = getSignalHistoryBuffersPtr<T>( signal.signalID );

    if ( signalHistoryBufferVectorPtr == nullptr )
    {
        return;
    }
    auto &signalHistoryBufferVector = *signalHistoryBufferVectorPtr;
    for ( auto &buffer : signalHistoryBufferVector )
    {
        // There is one buffer per sample interval ms for each signal
        if ( buffer.mMinimumSampleIntervalMs == signal.minimumSampleIntervalMs )
        {
            buffer.mSize = std::max( buffer.mSize, signal.sampleBufferSize );
            buffer.addFixedWindow( signal.fixedWindowPeriod );
            return;
        }
    }
    signalHistoryBufferVector.emplace_back( signal.sampleBufferSize,
                                            signal.minimumSampleIntervalMs
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                            ,
                                            signal.signalType == SignalType::COMPLEX_SIGNAL
#endif
    );
    signalHistoryBufferVector.back().addFixedWindow( signal.fixedWindowPeriod );
}

void
CollectionInspectionEngine::onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix,
                                                      const TimePoint &currentTime )
{
    // Clears everything in this class including all data in the signal history buffer
    clear();

    // Default all conditions to true to force rising edge logic
    mConditionsWithConditionCurrentlyTrue.set();

    mActiveInspectionMatrix = inspectionMatrix; // Pointers and references into this memory are maintained so hold
                                                // a shared_ptr to it so it does not get deleted
    for ( auto &condition : mActiveInspectionMatrix->conditions )
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
        if ( condition.signals.size() > MAX_DIFFERENT_SIGNAL_IDS )
        {
            TraceModule::get().incrementVariable( TraceVariable::CE_SIGNAL_ID_OUTBOUND );
            FWE_LOG_ERROR( "There can be only " + std::to_string( MAX_DIFFERENT_SIGNAL_IDS ) +
                           " different signal IDs" );
            TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
            return;
        }
        mConditions.emplace_back( condition );
        for ( auto &signal : condition.signals )
        {
            if ( signal.signalID == INVALID_SIGNAL_ID )
            {
                FWE_LOG_ERROR( "A SignalID with value " + std::to_string( INVALID_SIGNAL_ID ) + " is not allowed" );
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
                return;
            }
            if ( signal.sampleBufferSize == 0 )
            {
                TraceModule::get().incrementVariable( TraceVariable::CE_SAMPLE_SIZE_ZERO );
                FWE_LOG_ERROR( "A Sample buffer size of 0 is not allowed" );
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
                return;
            }
            if ( signal.signalType == SignalType::UNKNOWN )
            {
                FWE_LOG_WARN( "Signal ID: " + std::to_string( signal.signalID ) + " associated with Campaign SyncId " +
                              ( condition.metadata.collectionSchemeID ) +
                              " is of type unknown and should not be processed" );
                continue;
            }

            mSignalToBufferTypeMap.insert( { signal.signalID, signal.signalType } );
            switch ( signal.signalType )
            {
            case SignalType::UINT8:
                addSignalBuffer<uint8_t>( signal );
                break;
            case SignalType::INT8:
                addSignalBuffer<int8_t>( signal );
                break;
            case SignalType::UINT16:
                addSignalBuffer<uint16_t>( signal );
                break;
            case SignalType::INT16:
                addSignalBuffer<int16_t>( signal );
                break;
            case SignalType::UINT32:
                addSignalBuffer<uint32_t>( signal );
                break;
            case SignalType::INT32:
                addSignalBuffer<int32_t>( signal );
                break;
            case SignalType::UINT64:
                addSignalBuffer<uint64_t>( signal );
                break;
            case SignalType::INT64:
                addSignalBuffer<int64_t>( signal );
                break;
            case SignalType::FLOAT:
                addSignalBuffer<float>( signal );
                break;
            case SignalType::DOUBLE:
                addSignalBuffer<double>( signal );
                break;
            case SignalType::BOOLEAN:
                addSignalBuffer<bool>( signal );
                break;
            case SignalType::UNKNOWN:
                // Signal of UNKNOWN type should not be processed
                break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            case SignalType::COMPLEX_SIGNAL:
                addSignalBuffer<RawData::BufferHandle>( signal );
                break;
#endif
            }
        }
        for ( auto &canFrame : condition.canFrames )
        {
            bool found = false;
            for ( auto &buf : mCanFrameBuffers )
            {
                if ( ( buf.mFrameID == canFrame.frameID ) && ( buf.mChannelID == canFrame.channelID ) &&
                     ( buf.mMinimumSampleIntervalMs == canFrame.minimumSampleIntervalMs ) )
                {
                    found = true;
                    buf.mSize = std::max( buf.mSize, canFrame.sampleBufferSize );
                    break;
                }
            }
            if ( !found )
            {
                mCanFrameBuffers.emplace_back(
                    canFrame.frameID, canFrame.channelID, canFrame.sampleBufferSize, canFrame.minimumSampleIntervalMs );
            }
        }
    }
    // At this point all buffers should be resized to correct size. Now pointer to std::vector elements can be used
    for ( uint32_t conditionIndex = 0; conditionIndex < mConditions.size(); conditionIndex++ )
    {
        auto &activeCondition = mConditions[conditionIndex];

        if ( activeCondition.mCondition.isStaticCondition )
        {
            // evaluate static condition once when inspection matrix is handed over
            evaluateStaticCondition( conditionIndex );
        }

        for ( auto &signal : activeCondition.mCondition.signals )
        {
            switch ( signal.signalType )
            {
            case SignalType::UINT8:
                updateConditionBuffer<uint8_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::INT8:
                updateConditionBuffer<int8_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::UINT16:
                updateConditionBuffer<uint16_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::INT16:
                updateConditionBuffer<int16_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::UINT32:
                updateConditionBuffer<uint32_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::INT32:
                updateConditionBuffer<int32_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::UINT64:
                updateConditionBuffer<uint64_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::INT64:
                updateConditionBuffer<int64_t>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::FLOAT:
                updateConditionBuffer<float>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::DOUBLE:
                updateConditionBuffer<double>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::BOOLEAN:
                updateConditionBuffer<bool>( signal, activeCondition, conditionIndex );
                break;
            case SignalType::UNKNOWN:
                // Signal of type UNKNOWN should not be processed
                break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            case SignalType::COMPLEX_SIGNAL:
                updateConditionBuffer<RawData::BufferHandle>( signal, activeCondition, conditionIndex );
                break;
#endif
            }
        }
        // Overwrite last trigger time 0 with current time to avoid trigger at time 0
        activeCondition.mLastTrigger = currentTime;

        for ( const auto &signal : activeCondition.mCondition.signals )
        {
            if ( !signal.isConditionOnlySignal )
            {
                activeCondition.mCollectedSignalIds.emplace( signal.signalID );
            }
        }
    }

    static_cast<void>( preAllocateBuffers() );
}

template <typename T>
void
CollectionInspectionEngine::updateConditionBuffer(
    const InspectionMatrixSignalCollectionInfo &inspectionMatrixCollectionInfo,
    ActiveCondition &activeCondition,
    const long unsigned int conditionIndex )
{
    SignalID signalID = inspectionMatrixCollectionInfo.signalID;
    auto buf = getSignalHistoryBufferPtr<T>( signalID, inspectionMatrixCollectionInfo.minimumSampleIntervalMs );
    if ( buf != nullptr )
    {
        buf->mConditionsThatEvaluateOnThisSignal.set( conditionIndex );
        activeCondition.mConditionSignals.insert( { signalID, buf } );
        FixedTimeWindowFunctionData<T> *window =
            buf->getFixedWindow( inspectionMatrixCollectionInfo.fixedWindowPeriod );
        if ( window != nullptr )
        {
            // activeCondition.mEvaluationFunctions[signalID] = window;
            activeCondition.mEvaluationFunctions.insert( { signalID, window } );
        }
    }
}

template <typename T>
bool
CollectionInspectionEngine::allocateBufferVector( SignalID signalID, size_t &usedBytes )
{
    auto signalHistoryBufferVectorPtr = getSignalHistoryBuffersPtr<T>( signalID );
    if ( signalHistoryBufferVectorPtr != nullptr )
    {
        auto &signalHistoryBufferVector = *signalHistoryBufferVectorPtr;
        for ( auto &buffer : signalHistoryBufferVector )
        {
            uint64_t requiredBytes = buffer.mSize * static_cast<uint64_t>( sizeof( struct SignalSample<T> ) );
            if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
            {
                FWE_LOG_WARN( "The requested " + std::to_string( buffer.mSize ) +
                              " number of signal samples leads to a memory requirement  that's above the maximum "
                              "configured of " +
                              std::to_string( MAX_SAMPLE_MEMORY ) + "Bytes" );
                buffer.mSize = 0;
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
                return false;
            }
            usedBytes += static_cast<size_t>( requiredBytes );

            // reserve the size like new[]
            buffer.mBuffer.resize( buffer.mSize );
        }
    }
    return true;
}

bool
CollectionInspectionEngine::preAllocateBuffers()
{
    // Allocate size
    size_t usedBytes = 0;

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
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            case SignalType::COMPLEX_SIGNAL:
                if ( !allocateBufferVector<RawData::BufferHandle>( signalID, usedBytes ) )
                {
                    return false;
                }
                break;
#endif
            default:
                FWE_LOG_WARN( "Unknown type :" + std::to_string( static_cast<uint32_t>( signalType ) ) );
                break;
            }
        }
    }
    // Allocate Can buffer
    for ( auto &buf : mCanFrameBuffers )
    {
        size_t requiredBytes = buf.mSize * sizeof( struct CanFrameSample );
        if ( usedBytes + requiredBytes > MAX_SAMPLE_MEMORY )
        {
            FWE_LOG_WARN( "The requested " + std::to_string( buf.mSize ) +
                          " number of CAN raw samples leads to a memory requirement  that's above the maximum "
                          "configured of" +
                          std::to_string( MAX_SAMPLE_MEMORY ) + "Bytes" );
            buf.mSize = 0;
            return false;
        }
        usedBytes += requiredBytes;

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
    mConditionsTriggeredWaitingPublished.reset();
    if ( mRawBufferManager != nullptr )
    {
        mRawBufferManager->resetUsageHintsForStage(
            RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER );
    }
}

template <typename T>
void
CollectionInspectionEngine::updateBufferFixedWindowFunction( SignalID signalID, Timestamp timestamp )
{
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    try
    {
        if ( mSignalBuffers.find( signalID ) != mSignalBuffers.end() )
        {
            auto &mapVal = mSignalBuffers.at( signalID );
            signalHistoryBufferPtr = boost::get<std::vector<SignalHistoryBuffer<T>>>( &mapVal );
        }
    }
    catch ( ... )
    {
        FWE_LOG_ERROR( "Failed to retrieve signalHistoryBuffer vector for signal ID " + std::to_string( signalID ) );
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
CollectionInspectionEngine::updateAllFixedWindowFunctions( Timestamp timestamp )
{
    mNextWindowFunctionTimesOut = std::numeric_limits<Timestamp>::max();
    for ( auto &signalVector : mSignalBuffers )
    {
        auto signalID = signalVector.first;
        if ( mSignalToBufferTypeMap.find( signalID ) != mSignalToBufferTypeMap.end() )
        {
            auto signalType = mSignalToBufferTypeMap[signalID];
            // coverity[autosar_cpp14_m6_4_6_violation]
            // coverity[misra_cpp_2008_rule_6_4_6_violation] compiler warning is preferred over a default-clause
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
            case SignalType::UNKNOWN:
                FWE_LOG_WARN( "Window functions are not supported for signal ID: " + std::to_string( signalID ) +
                              " as it is of type UNKNOWN" );
                break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            case SignalType::COMPLEX_SIGNAL:
                // Window functions are not supported for complex signals
                break;
#endif
            }
        }
    }
}

void
CollectionInspectionEngine::evaluateStaticCondition( uint32_t conditionIndex )
{
    ActiveCondition &condition = mConditions[conditionIndex];
    InspectionValue result;
    ExpressionErrorCode ret =
        eval( condition.mCondition.condition, condition, result, MAX_EQUATION_DEPTH, conditionIndex );
    if ( ( ret != ExpressionErrorCode::SUCCESSFUL ) || ( result.type != InspectionValue::DataType::BOOL ) ||
         ( !result.boolVal ) )
    {
        // Flip default true flag to false if static condition is evaluated to false
        mConditionsWithConditionCurrentlyTrue.reset( conditionIndex );
    }
}

bool
CollectionInspectionEngine::evaluateConditions( const TimePoint &currentTime )
{
    bool oneConditionEvaluatedToTrue = false;

    // if any sampling window times out there is a new value available to be processed by a condition
    if ( currentTime.monotonicTimeMs >= mNextWindowFunctionTimesOut )
    {
        updateAllFixedWindowFunctions( currentTime.monotonicTimeMs );
    }

    // faster implementation like find next bit set to one would be possible but for example
    // conditionsToEvaluate._Find_first is not part of C++ standard
    for ( uint32_t i = 0; i < mConditions.size(); i++ )
    {
        ActiveCondition &condition = mConditions[i];
        bool conditionEvaluatedToTrue = false;
        // Only reevaluate non-static conditions with changed input
        if ( ( ( mConditionsWithInputSignalChanged.test( i ) ) && ( !condition.mCondition.isStaticCondition ) ) ||
             ( condition.mCondition.alwaysEvaluateCondition ) )
        {
            InspectionValue result;
            ExpressionErrorCode ret = eval( condition.mCondition.condition, condition, result, MAX_EQUATION_DEPTH, i );
            if ( ( ret != ExpressionErrorCode::SUCCESSFUL ) || ( !result.isBoolOrDouble() ) || ( !result.asBool() ) )
            {
                mConditionsWithConditionCurrentlyTrue.reset( i );
            }
            else
            {
                conditionEvaluatedToTrue = true;
            }
            mConditionsWithInputSignalChanged.reset( i );
        }
        // If condition was reevaluated to true or if condition is still true and not waiting to be published
        // Check if condition can be "retriggered" and marked for the upload
        if ( ( conditionEvaluatedToTrue ) || ( ( mConditionsWithConditionCurrentlyTrue.test( i ) ) &&
                                               ( !mConditionsTriggeredWaitingPublished.test( i ) ) ) )
        {
            // Mark conditions for upload only if minimumPublishIntervalMs has passed
            if ( currentTime.monotonicTimeMs >=
                 condition.mLastTrigger.monotonicTimeMs + condition.mCondition.minimumPublishIntervalMs )
            {
                if ( ( ( !condition.mCondition.triggerOnlyOnRisingEdge ) ||
                       ( !mConditionsWithConditionCurrentlyTrue.test( i ) ) ) &&
                     ( !mConditionsTriggeredWaitingPublished.test( i ) ) )
                {
                    // Mark condition for the upload
                    mConditionsTriggeredWaitingPublished.set( i );
                    condition.mLastTrigger = currentTime;
                    // Prepare the collected data, but don't fill it from the signal history buffers yet
                    condition.mCollectedData = { std::make_shared<TriggeredCollectionSchemeData>(),
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                                 std::make_shared<TriggeredVisionSystemData>()
#endif
                    };
                }
                mConditionsWithConditionCurrentlyTrue.set( i );
                oneConditionEvaluatedToTrue = true;
            }
        }
    }
    return oneConditionEvaluatedToTrue;
}

template <typename T>
void
CollectionInspectionEngine::collectLastSignals( SignalID id,
                                                size_t maxNumberOfSignalsToCollect,
                                                uint32_t conditionId,
                                                SignalType signalType,
                                                Timestamp &newestSignalTimestamp,
                                                std::vector<CollectedSignal> &output )
{
    auto buf = mConditions[conditionId].getEvaluationSignalsBufferPtr<T>( id );
    if ( buf == nullptr )
    {
        // Signal not collected by any active condition or access by Invalid DataType
        return;
    }
    int pos = static_cast<int>( buf->mCurrentPosition );
    for ( size_t i = 0; i < std::min( maxNumberOfSignalsToCollect, buf->mCounter ); i++ )
    {
        // Ensure access is in bounds
        if ( pos < 0 )
        {
            pos = static_cast<int>( buf->mSize ) - 1;
        }
        if ( pos >= static_cast<int>( buf->mSize ) )
        {
            pos = 0;
        }
        auto &sample = buf->mBuffer[static_cast<uint32_t>( pos )];
        if ( ( !sample.isAlreadyConsumed( conditionId ) ) || ( !mSendDataOnlyOncePerCondition ) )
        {
            output.emplace_back( id, sample.mTimestamp, sample.mValue, signalType );
            sample.setAlreadyConsumed( conditionId, true );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            if ( signalType == SignalType::COMPLEX_SIGNAL )
            {
                NotifyRawBufferManager<T>::increaseElementUsage(
                    id,
                    mRawBufferManager.get(),
                    RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD,
                    sample.mValue );
            }
#endif
        }
        newestSignalTimestamp = std::max( newestSignalTimestamp, sample.mTimestamp );
        pos--;
    }
}

void
CollectionInspectionEngine::collectLastCanFrames( CANRawFrameID canID,
                                                  CANChannelNumericID channelID,
                                                  uint32_t minimumSamplingInterval,
                                                  size_t maxNumberOfSignalsToCollect,
                                                  uint32_t conditionId,
                                                  Timestamp &newestSignalTimestamp,
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

void
CollectionInspectionEngine::collectData( ActiveCondition &condition,
                                         uint32_t conditionId,
                                         Timestamp &newestSignalTimestamp,
                                         CollectionInspectionEngineOutput &output )
{
    output.triggeredCollectionSchemeData->metadata = condition.mCondition.metadata;
    output.triggeredCollectionSchemeData->triggerTime = condition.mLastTrigger.systemTimeMs;
    output.triggeredCollectionSchemeData->eventID = condition.mEventID;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    output.triggeredVisionSystemData->metadata = condition.mCondition.metadata;
    output.triggeredVisionSystemData->triggerTime = condition.mLastTrigger.systemTimeMs;
    output.triggeredVisionSystemData->eventID = condition.mEventID;
#endif
    // Pack signals
    for ( auto &s : condition.mCondition.signals )
    {
        if ( !s.isConditionOnlySignal )
        {
            switch ( s.signalType )
            {
            case SignalType::UINT8:
                collectLastSignals<uint8_t>( s.signalID,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::INT8:
                collectLastSignals<int8_t>( s.signalID,
                                            s.sampleBufferSize,
                                            conditionId,
                                            s.signalType,
                                            newestSignalTimestamp,
                                            output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::UINT16:
                collectLastSignals<uint16_t>( s.signalID,
                                              s.sampleBufferSize,
                                              conditionId,
                                              s.signalType,
                                              newestSignalTimestamp,
                                              output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::INT16:
                collectLastSignals<int16_t>( s.signalID,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::UINT32:
                collectLastSignals<uint32_t>( s.signalID,
                                              s.sampleBufferSize,
                                              conditionId,
                                              s.signalType,
                                              newestSignalTimestamp,
                                              output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::INT32:
                collectLastSignals<int32_t>( s.signalID,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::UINT64:
                collectLastSignals<uint64_t>( s.signalID,
                                              s.sampleBufferSize,
                                              conditionId,
                                              s.signalType,
                                              newestSignalTimestamp,
                                              output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::INT64:
                collectLastSignals<int64_t>( s.signalID,
                                             s.sampleBufferSize,
                                             conditionId,
                                             s.signalType,
                                             newestSignalTimestamp,
                                             output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::FLOAT:
                collectLastSignals<float>( s.signalID,
                                           s.sampleBufferSize,
                                           conditionId,
                                           s.signalType,
                                           newestSignalTimestamp,
                                           output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::DOUBLE:
                collectLastSignals<double>( s.signalID,
                                            s.sampleBufferSize,
                                            conditionId,
                                            s.signalType,
                                            newestSignalTimestamp,
                                            output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::BOOLEAN:
                collectLastSignals<bool>( s.signalID,
                                          s.sampleBufferSize,
                                          conditionId,
                                          s.signalType,
                                          newestSignalTimestamp,
                                          output.triggeredCollectionSchemeData->signals );
                break;
            case SignalType::UNKNOWN:
                FWE_LOG_WARN( "Signal ID: " + std::to_string( s.signalID ) + " associated with Campaign SyncId: " +
                              ( condition.mCondition.metadata.collectionSchemeID ) +
                              " is of UNKNOWN type and will not be collected" );
                break;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            case SignalType::COMPLEX_SIGNAL:
                collectLastSignals<RawData::BufferHandle>( s.signalID,
                                                           s.sampleBufferSize,
                                                           conditionId,
                                                           s.signalType,
                                                           newestSignalTimestamp,
                                                           output.triggeredVisionSystemData->signals );
                break;
#endif
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
                              output.triggeredCollectionSchemeData->canFrames );
    }
    // Pack active DTCs if any
    if ( condition.mCondition.includeActiveDtcs &&
         ( ( !isActiveDTCsConsumed( conditionId ) ) || mSendDataOnlyOncePerCondition ) )
    {
        output.triggeredCollectionSchemeData->mDTCInfo = mActiveDTCs;
        setActiveDTCsConsumed( conditionId, true );
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    if ( output.triggeredVisionSystemData->signals.empty() )
    {
        output.triggeredVisionSystemData = nullptr;
    }
#endif
}

CollectionInspectionEngineOutput
CollectionInspectionEngine::collectNextDataToSend( const TimePoint &currentTime, uint32_t &waitTimeMs )
{
    uint32_t minimumWaitTimeMs = std::numeric_limits<uint32_t>::max();
    if ( mConditionsTriggeredWaitingPublished.none() )
    {
        waitTimeMs = minimumWaitTimeMs;
        return {};
    }
    for ( uint32_t i = 0; i < mConditions.size(); i++ )
    {
        if ( mNextConditionToCollectedIndex >= mConditions.size() )
        {
            mNextConditionToCollectedIndex = 0;
        }
        if ( mConditionsTriggeredWaitingPublished.test( mNextConditionToCollectedIndex ) )
        {
            auto &condition = mConditions[mNextConditionToCollectedIndex];
            {
                if ( ( ( condition.mLastTrigger.systemTimeMs == 0 ) &&
                       ( condition.mLastTrigger.monotonicTimeMs == 0 ) ) ||
                     ( currentTime.monotonicTimeMs >=
                       condition.mLastTrigger.monotonicTimeMs + condition.mCondition.afterDuration ) )
                {
                    // Mark as not triggered since data is going to be collected
                    mConditionsTriggeredWaitingPublished.reset( mNextConditionToCollectedIndex );
                    // Generate the Event ID and pack  it into the active Condition
                    condition.mEventID = generateEventID( currentTime.systemTimeMs );
                    // Return the collected data
                    Timestamp newestSignalTimeStamp = 0;
                    collectData(
                        condition, mNextConditionToCollectedIndex, newestSignalTimeStamp, condition.mCollectedData );
                    // After collecting the data set the newest timestamp from any data that was
                    // collected
                    condition.mLastDataTimestampPublished =
                        std::min( newestSignalTimeStamp, currentTime.monotonicTimeMs );
                    // Increase index before returning from the function
                    mNextConditionToCollectedIndex++;
                    return std::move( condition.mCollectedData );
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
        }
        mNextConditionToCollectedIndex++;
    }
    // No Data ready to be sent
    waitTimeMs = minimumWaitTimeMs;
    return {};
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
ExpressionErrorCode
CollectionInspectionEngine::getLatestBufferSignalValue( SignalID id,
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

ExpressionErrorCode
CollectionInspectionEngine::getLatestSignalValue( SignalID id, ActiveCondition &condition, InspectionValue &result )
{
    if ( mSignalToBufferTypeMap.find( id ) == mSignalToBufferTypeMap.end() )
    {
        FWE_LOG_WARN( "SIGNAL_NOT_FOUND" );
        // Signal not collected by any active condition
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
    }
    auto signalType = mSignalToBufferTypeMap[id];
    // coverity[autosar_cpp14_m6_4_6_violation]
    // coverity[misra_cpp_2008_rule_6_4_6_violation] compiler warning is preferred over a default-clause
    switch ( signalType )
    {
    case SignalType::UINT8:
        return getLatestBufferSignalValue<uint8_t>( id, condition, result );
    case SignalType::INT8:
        return getLatestBufferSignalValue<int8_t>( id, condition, result );
    case SignalType::UINT16:
        return getLatestBufferSignalValue<uint16_t>( id, condition, result );
    case SignalType::INT16:
        return getLatestBufferSignalValue<uint16_t>( id, condition, result );
    case SignalType::UINT32:
        return getLatestBufferSignalValue<uint32_t>( id, condition, result );
    case SignalType::INT32:
        return getLatestBufferSignalValue<int32_t>( id, condition, result );
    case SignalType::UINT64:
        return getLatestBufferSignalValue<uint64_t>( id, condition, result );
    case SignalType::INT64:
        return getLatestBufferSignalValue<int64_t>( id, condition, result );
    case SignalType::FLOAT:
        return getLatestBufferSignalValue<float>( id, condition, result );
    case SignalType::DOUBLE:
        return getLatestBufferSignalValue<double>( id, condition, result );
    case SignalType::BOOLEAN:
        return getLatestBufferSignalValue<bool>( id, condition, result );
    case SignalType::UNKNOWN:
        FWE_LOG_WARN( "Signal ID: " + std::to_string( id ) + " associated with Campaign SyncId: " +
                      condition.mCondition.metadata.collectionSchemeID + " is of type UNKNOWN and used in evaluation" );
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    case SignalType::COMPLEX_SIGNAL:
        FWE_LOG_WARN( "Complex signals are not supported in evaluation" )
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
#endif
    }
    return ExpressionErrorCode::SIGNAL_NOT_FOUND;
}

template <typename T>
ExpressionErrorCode
CollectionInspectionEngine::getSampleWindowFunctionType( WindowFunction function,
                                                         SignalID signalID,
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

ExpressionErrorCode
CollectionInspectionEngine::getSampleWindowFunction( WindowFunction function,
                                                     SignalID signalID,
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
    // coverity[autosar_cpp14_m6_4_6_violation]
    // coverity[misra_cpp_2008_rule_6_4_6_violation] compiler warning is preferred over a default-clause
    switch ( signalType )
    {
    case SignalType::UINT8:
        return getSampleWindowFunctionType<uint8_t>( function, signalID, condition, result );
    case SignalType::INT8:
        return getSampleWindowFunctionType<int8_t>( function, signalID, condition, result );
    case SignalType::UINT16:
        return getSampleWindowFunctionType<uint16_t>( function, signalID, condition, result );
    case SignalType::INT16:
        return getSampleWindowFunctionType<int16_t>( function, signalID, condition, result );
    case SignalType::UINT32:
        return getSampleWindowFunctionType<uint32_t>( function, signalID, condition, result );
    case SignalType::INT32:
        return getSampleWindowFunctionType<int32_t>( function, signalID, condition, result );
    case SignalType::UINT64:
        return getSampleWindowFunctionType<uint64_t>( function, signalID, condition, result );
    case SignalType::INT64:
        return getSampleWindowFunctionType<int64_t>( function, signalID, condition, result );
    case SignalType::FLOAT:
        return getSampleWindowFunctionType<float>( function, signalID, condition, result );
    case SignalType::DOUBLE:
        return getSampleWindowFunctionType<double>( function, signalID, condition, result );
    case SignalType::BOOLEAN:
        return getSampleWindowFunctionType<bool>( function, signalID, condition, result );
    case SignalType::UNKNOWN:
        FWE_LOG_WARN( "Window functions are not supported for signal ID: " + std::to_string( signalID ) +
                      " associated with Campaign SyncId: " + condition.mCondition.metadata.collectionSchemeID +
                      " as signal is of type UNKNOWN" );
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    case SignalType::COMPLEX_SIGNAL:
        FWE_LOG_WARN( "Window functions are not supported for complex signals" )
        return ExpressionErrorCode::SIGNAL_NOT_FOUND;
#endif
    }
    return ExpressionErrorCode::SIGNAL_NOT_FOUND;
}

ExpressionErrorCode
CollectionInspectionEngine::eval( const ExpressionNode *expression,
                                  ActiveCondition &condition,
                                  InspectionValue &resultValue,
                                  int remainingStackDepth,
                                  uint32_t conditionId )
{
    if ( ( remainingStackDepth <= 0 ) || ( expression == nullptr ) )
    {
        FWE_LOG_WARN( "STACK_DEPTH_REACHED or nullptr" );
        return ExpressionErrorCode::STACK_DEPTH_REACHED;
    }
    if ( expression->nodeType == ExpressionNodeType::FLOAT )
    {
        resultValue = expression->floatingValue;
        return ExpressionErrorCode::SUCCESSFUL;
    }
    if ( expression->nodeType == ExpressionNodeType::BOOLEAN )
    {
        resultValue = expression->booleanValue;
        return ExpressionErrorCode::SUCCESSFUL;
    }
    if ( expression->nodeType == ExpressionNodeType::SIGNAL )
    {
        return getLatestSignalValue( expression->signalID, condition, resultValue );
    }
    if ( expression->nodeType == ExpressionNodeType::WINDOW_FUNCTION )
    {
        return getSampleWindowFunction(
            expression->function.windowFunction, expression->signalID, condition, resultValue );
    }

    InspectionValue leftResult;
    InspectionValue rightResult;
    ExpressionErrorCode leftRet = ExpressionErrorCode::SUCCESSFUL;
    ExpressionErrorCode rightRet = ExpressionErrorCode::SUCCESSFUL;
    // Recursion limited depth through last parameter
    leftRet = eval( expression->left, condition, leftResult, remainingStackDepth - 1, conditionId );

    if ( leftRet != ExpressionErrorCode::SUCCESSFUL )
    {
        return leftRet;
    }

    // Logical NOT operator does not have a right operand, hence expression->right can be nullptr
    if ( expression->nodeType != ExpressionNodeType::OPERATOR_LOGICAL_NOT )
    {
        // No short-circuit evaluation so always evaluate right part
        rightRet = eval( expression->right, condition, rightResult, remainingStackDepth - 1, conditionId );

        if ( rightRet != ExpressionErrorCode::SUCCESSFUL )
        {
            return rightRet;
        }
    }

    switch ( expression->nodeType )
    {
    case ExpressionNodeType::OPERATOR_SMALLER:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() < rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_BIGGER:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() > rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_SMALLER_EQUAL:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() <= rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_BIGGER_EQUAL:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() >= rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_EQUAL:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        else
        {
            resultValue = std::abs( leftResult.asDouble() - rightResult.asDouble() ) < EVAL_EQUAL_DISTANCE();
        }
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_NOT_EQUAL:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        else
        {
            resultValue = !( std::abs( leftResult.asDouble() - rightResult.asDouble() ) < EVAL_EQUAL_DISTANCE() );
        }
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_LOGICAL_AND:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asBool() && rightResult.asBool();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_LOGICAL_OR:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asBool() || rightResult.asBool();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_LOGICAL_NOT:
        if ( !leftResult.isBoolOrDouble() )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = !leftResult.asBool();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        else
        {
            resultValue = leftResult.asDouble() + rightResult.asDouble();
        }
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() - rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_MULTIPLY:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() * rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    case ExpressionNodeType::OPERATOR_ARITHMETIC_DIVIDE:
        if ( ( !leftResult.isBoolOrDouble() ) || ( !rightResult.isBoolOrDouble() ) )
        {
            return ExpressionErrorCode::TYPE_MISMATCH;
        }
        resultValue = leftResult.asDouble() / rightResult.asDouble();
        return ExpressionErrorCode::SUCCESSFUL;
    default:
        return ExpressionErrorCode::NOT_IMPLEMENTED_TYPE;
    }
}

EventID
CollectionInspectionEngine::generateEventID( Timestamp timestamp )
{
    // Generate an eventId as a combination of an event counter and a timestamp
    uint32_t eventId = static_cast<uint32_t>( generateEventCounter() ) | static_cast<uint32_t>( timestamp << 8 );
    // As Kotlin reads eventId as int32, set most significant bit to 0 so event IDs stay positive
    eventId = eventId & 0x7FFFFFFF;
    return eventId;
}

} // namespace IoTFleetWise
} // namespace Aws
