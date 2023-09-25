// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANDataTypes.h"
#include "CollectionInspectionAPITypes.h"
#include "DataReduction.h"
#include "EventTypes.h"
#include "GeohashFunctionNode.h"
#include "IActiveConditionProcessor.h"
#include "ICollectionScheme.h"
#include "InspectionEventListener.h"
#include "Listener.h"
#include "LoggingModule.h"
#include "MessageTypes.h"
#include "OBDDataTypes.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset> // As _Find_first() is not part of C++ standard and compiler specific other structure could be considered
#include <boost/variant.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using InspectionSignalID = uint32_t;

/**
 * @brief Main class to implement collection and inspection engine logic
 *
 * This class is not multithreading safe to the caller needs to ensure that the different functions
 * are called only from one thread. This class will be instantiated and used from the Collection
 * Inspection Engine thread
 */
class CollectionInspectionEngine : public IActiveConditionProcessor, public ThreadListeners<InspectionEventListener>
{

public:
    using InspectionTimestamp = uint64_t;
    using InspectionValue = double;

    /**
     * @brief Construct the CollectionInspectionEngine which handles multiple conditions
     *
     * @param sendDataOnlyOncePerCondition if true only data with a millisecond timestamp, bigger than the timestamp the
     * condition last sent out data, will be included.
     */
    CollectionInspectionEngine( bool sendDataOnlyOncePerCondition = true );

    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix ) override;

    /**
     * @brief Go through all conditions with changed condition signals and evaluate condition
     *
     * This needs to be called directly after new signals are added to the CollectionEngine.
     * If multiple samples of the same signal are added to CollectionEngine without calling
     * this function data can get lost and triggers can get missed.
     * @return at least one condition is true
     *
     */
    bool evaluateConditions( const TimePoint &currentTime );

    /**
     * @brief Copy for a triggered condition data out of the signal buffer
     *
     * It will copy the data the next triggered condition wants to publish out of
     * the signal history buffer.
     * It will allocate new memory for the data and return a shared ptr to it.
     * This data can then be passed on to be serialized and sent to the cloud.
     * Should be called after dataReadyToBeSent() true an shortly after adding new signals
     *
     * @param currentTime used to compare if the afterTime is over
     * @param waitTimeMs if no condition is ready this value will be set to the duration still necessary to wait
     *
     * @return if dataReadyToBeSent() is false a nullptr otherwise the collected data will be returned
     */
    std::shared_ptr<const TriggeredCollectionSchemeData> collectNextDataToSend( const TimePoint &currentTime,
                                                                                uint32_t &waitTimeMs );
    /**
     * @brief Give a new signal to the collection engine to cached it
     *
     * This API should be used for CAN Signals or OBD signals.
     * As multiple threads use this function it can wait on a mutex but as all critical sections
     * are small it should be fast.
     * If the signal is not needed by any condition it will directly be discarded
     * The signals should come in ordered by time (oldest signals first)
     *
     * @param id id of the obd based or can based signal
     * @param receiveTime timestamp at which time was the signal seen on the physical bus
     * @param value the signal value
     */
    template <typename T>
    void addNewSignal( InspectionSignalID id, const TimePoint &receiveTime, T value );

    template <typename T = double>
    void addSignalToBuffer( const InspectionMatrixSignalCollectionInfo &signalIn );

    /**
     * @brief Add new raw CAN Frame history buffer. If frame is not needed call will be just ignored
     *
     * This raw can frame will not be decoded they will be only stored and transmitted as raw can
     * frames to the cloud if cloud is interested in them.
     *
     * @param canID the message ID of the raw can frame seen on the bus
     * @param channelID the internal channel id on which the can frame was seen
     * @param receiveTime timestamp at which time was the can frame was seen on the physical bus
     * @param buffer raw byte buffer to the can frame
     * @param size size of the buffer in number of bytes
     *
     */
    void addNewRawCanFrame( CANRawFrameID canID,
                            CANChannelNumericID channelID,
                            const TimePoint &receiveTime,
                            std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> &buffer,
                            uint8_t size );

    /**
     * @brief Copies the data reduction object over and applies it before handing out data
     *
     * After setting this depending on the parameters the number of data packages collected over
     * collectNextDataToSend will go or down
     */
    void
    setDataReductionParameters( bool disableProbability )
    {
        mDataReduction.setDisableProbability( disableProbability );
    };

    void setActiveDTCs( const DTCInfo &activeDTCs );

private:
    static const uint32_t MAX_SAMPLE_MEMORY = 20 * 1024 * 1024; // 20MB max for all samples
    static inline InspectionValue
    EVAL_EQUAL_DISTANCE()
    {
        return 0.001;
    } // because static const double (non-integral type) not possible

    struct SampleConsumed
    {
        bool
        isAlreadyConsumed( uint32_t conditionId )
        {
            return mAlreadyConsumed.test( conditionId );
        }
        void
        setAlreadyConsumed( uint32_t conditionId, bool value )
        {
            if ( conditionId == ALL_CONDITIONS )
            {
                if ( value )
                {
                    mAlreadyConsumed.set();
                }
                else
                {
                    mAlreadyConsumed.reset();
                }
            }
            else
            {
                mAlreadyConsumed[conditionId] = value;
            }
        }

    private:
        std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION> mAlreadyConsumed{ 0 };
    };
    template <typename T = double>
    struct SignalSample : SampleConsumed
    {
        T mValue;
        InspectionTimestamp mTimestamp{ 0 };
    };

    struct CanFrameSample : SampleConsumed
    {
        uint8_t mSize{ 0 }; /**< elements in buffer variable used. So if the raw can messages is only 3 bytes big this
                           uint8_t will be 3 and only the first three bytes of buffer will contain meaningful data. size
                           optimized sizeof(size)+sizeof(buffer) = 9 < sizeof(std:vector) = 24 so also for empty
                           messages its mor efficient to preallocate 8 bytes even if not all will be used*/
        std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> mBuffer{};
        InspectionTimestamp mTimestamp{ 0 };
    };

    /**
     * @brief maintains values like avg, min or max calculated over a certain time window
     *
     * All values here are calculated with an online algorithm, so everytime
     * there is a new value for a signal all internal values are updated. There is no
     * need to look at historic values. The window is time based and not sample based.
     * Currently the last 2 windows are maintained inside this class.
     * */
    template <typename T = double>
    class FixedTimeWindowFunctionData
    {
    public:
        FixedTimeWindowFunctionData( uint32_t size )
            : mWindowSizeMs( size )
        {
        }

        InspectionTimestamp mWindowSizeMs{ 0 };       /** <over which time is the window calculated */
        InspectionTimestamp mLastTimeCalculated{ 0 }; /** <the time the last window stopped*/

        // This value represent the newest window that was complete and calculated
        T mLastMin{ std::numeric_limits<T>::min() };
        T mLastMax{ std::numeric_limits<T>::max() };
        T mLastAvg{ 0 };
        bool mLastAvailable{ false }; /** <if this is false there is no data for this window. For example if the window
                                       * is 30 minutes long in the first 30 minutes of running FWE receiving the signal
                                       * no data is available
                                       */
        // This values represents the value before the newest window
        T mPreviousLastMin{ std::numeric_limits<T>::min() };
        T mPreviousLastMax{ std::numeric_limits<T>::max() };
        T mPreviousLastAvg{ 0 };
        bool mPreviousLastAvailable{ false };

        // This values are changed online with every new signal sample and will be used to calculate
        // the next window as soon as the window time is over
        T mCollectingMin{ std::numeric_limits<T>::min() };
        T mCollectingMax{ std::numeric_limits<T>::max() };
        double mCollectingSum{ 0 };
        uint32_t mCollectedSignals{ 0 };

        /**
         * @brief update fixed sample windows when fixed time is over
         *
         * @param timestamp will be used to detect if current window is over
         * @param nextWindowFunctionTimesOut will be reduced if this window ends earlier
         *
         * @return true if any window value changed false otherwise
         */
        bool updateWindow( InspectionTimestamp timestamp, InspectionTimestamp &nextWindowFunctionTimesOut );
        inline void
        addValue( T value, InspectionTimestamp timestamp, InspectionTimestamp &nextWindowFunctionTimesOut )
        {
            updateWindow( timestamp, nextWindowFunctionTimesOut );
            updateInternalVariables( value );
        }

    private:
        inline void
        updateInternalVariables( T value )
        {
            mCollectingMin = std::min( mCollectingMin, value );
            mCollectingMax = std::max( mCollectingMax, value );
            mCollectingSum += static_cast<double>( value );
            mCollectedSignals++;
        }
        inline void
        initNewWindow( InspectionTimestamp timestamp, InspectionTimestamp &nextWindowFunctionTimesOut )
        {
            mCollectingMin = std::numeric_limits<T>::max();
            mCollectingMax = std::numeric_limits<T>::min();
            mCollectingSum = 0;
            mCollectedSignals = 0;
            mLastTimeCalculated +=
                static_cast<uint32_t>( ( timestamp - mLastTimeCalculated ) / mWindowSizeMs ) * mWindowSizeMs;
            nextWindowFunctionTimesOut = std::min( nextWindowFunctionTimesOut, mLastTimeCalculated + mWindowSizeMs );
        }
    }; // end class FixedTimeWindowFunctionData

    /**
     * @brief stores the history of one signal.
     *
     * The signal can be used as part of a condition or only to be published in the case
     * a condition is true
     */
    template <typename T = double>
    struct SignalHistoryBuffer
    {
        SignalHistoryBuffer( uint32_t sizeIn, uint32_t sampleInterval )
            : mMinimumSampleIntervalMs( sampleInterval )
            , mSize( sizeIn )
            , mCurrentPosition( mSize - 1 )
        {
        }

        uint32_t mMinimumSampleIntervalMs{ 0 };
        std::vector<struct SignalSample<T>>
            mBuffer; // ringbuffer, Consider to move to raw pointer allocated with new[] if vector allocates too much
        uint32_t mSize{ 0 };            // minimum size needed by all conditions, buffer must be at least this big
        uint32_t mCurrentPosition{ 0 }; /**< position in ringbuffer needs to come after size as it depends on it */
        uint32_t mCounter{ 0 };         /**< over all recorded samples*/
        TimePoint mLastSample{ 0, 0 };
        std::vector<FixedTimeWindowFunctionData<T>>
            mWindowFunctionData; /**< every signal buffer can have multiple windows over different time periods*/
        std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
            mConditionsThatEvaluateOnThisSignal; /**< if bit 0 is set it means element with index 0 of vector conditions
                                                 needs to reevaluate if this signal changes*/

        inline FixedTimeWindowFunctionData<T> *
        addFixedWindow( uint32_t windowSizeMs )
        {
            if ( windowSizeMs == 0 )
            {
                return nullptr;
            }
            FixedTimeWindowFunctionData<T> *findWindow = getFixedWindow( windowSizeMs );
            if ( findWindow != nullptr )
            {
                return findWindow;
            }
            mWindowFunctionData.emplace_back( windowSizeMs );
            return &( mWindowFunctionData.back() );
        }

        inline FixedTimeWindowFunctionData<T> *
        getFixedWindow( uint32_t windowSizeMs )
        {
            if ( windowSizeMs == 0 )
            {
                return nullptr;
            }
            for ( auto &window : mWindowFunctionData )
            {
                if ( window.mWindowSizeMs == windowSizeMs )
                {
                    return &window;
                }
            }
            return nullptr;
        }
    };

    /**
     * @brief stores the history of a can frame which will be published raw
     *
     */
    struct CanFrameHistoryBuffer
    {
        CanFrameHistoryBuffer() = default;
        CanFrameHistoryBuffer( CANRawFrameID frameID,
                               CANChannelNumericID channelID,
                               uint32_t sizeIn,
                               uint32_t sampleInterval )
            : mFrameID( frameID )
            , mChannelID( channelID )
            , mMinimumSampleIntervalMs( sampleInterval )
            , mSize( sizeIn )
            , mCurrentPosition( mSize - 1 )
        {
        }
        CANRawFrameID mFrameID{ INVALID_CAN_FRAME_ID };
        CANChannelNumericID mChannelID{ INVALID_CAN_SOURCE_NUMERIC_ID };
        uint32_t mMinimumSampleIntervalMs{ 0 };
        std::vector<struct CanFrameSample>
            mBuffer; // ringbuffer, Consider to move to raw pointer allocated with new[] if vector allocates too much
        uint32_t mSize{ 0 };
        uint32_t mCurrentPosition{ mSize - 1 }; // position in ringbuffer
        uint32_t mCounter{ 0 };
        TimePoint mLastSample{ 0, 0 };
    };

    // VSS supported datatypes
    using signalHistoryBufferPtrVar = boost::variant<SignalHistoryBuffer<int64_t> *,
                                                     SignalHistoryBuffer<float> *,
                                                     SignalHistoryBuffer<bool> *,
                                                     SignalHistoryBuffer<uint8_t> *,
                                                     SignalHistoryBuffer<int8_t> *,
                                                     SignalHistoryBuffer<uint16_t> *,
                                                     SignalHistoryBuffer<int16_t> *,
                                                     SignalHistoryBuffer<uint32_t> *,
                                                     SignalHistoryBuffer<int32_t> *,
                                                     SignalHistoryBuffer<uint64_t> *,
                                                     SignalHistoryBuffer<double> *>;

    // VSS supported datatypes
    using FixedTimeWindowFunctionPtrVar = boost::variant<FixedTimeWindowFunctionData<int64_t> *,
                                                         FixedTimeWindowFunctionData<float> *,
                                                         FixedTimeWindowFunctionData<bool> *,
                                                         FixedTimeWindowFunctionData<uint8_t> *,
                                                         FixedTimeWindowFunctionData<int8_t> *,
                                                         FixedTimeWindowFunctionData<uint16_t> *,
                                                         FixedTimeWindowFunctionData<int16_t> *,
                                                         FixedTimeWindowFunctionData<uint32_t> *,
                                                         FixedTimeWindowFunctionData<int32_t> *,
                                                         FixedTimeWindowFunctionData<uint64_t> *,
                                                         FixedTimeWindowFunctionData<double> *>;

    /**
     * @brief Stores information specific to one condition like the last time if was true
     */
    struct ActiveCondition
    {
        ActiveCondition( const ConditionWithCollectedData &conditionIn )
            : mCondition( conditionIn )
        {
        }
        InspectionTimestamp mLastDataTimestampPublished{ 0 };
        TimePoint mLastTrigger{ 0, 0 };
        // TODO** :: Update the type here
        std::unordered_map<InspectionSignalID, signalHistoryBufferPtrVar>
            mEvaluationSignals; // for fast lookup signals used for evaluation
        std::unordered_map<InspectionSignalID, FixedTimeWindowFunctionPtrVar>
            mEvaluationFunctions; // for fast lookup functions used for evaluation
        const ConditionWithCollectedData &mCondition;
        // Unique Identifier of the Event matched by this condition.
        EventID mEventID{ 0 };

        template <typename T = double>
        SignalHistoryBuffer<T> *
        getEvaluationSignalsBufferPtr( InspectionSignalID signalIDIn )
        {
            auto evaluationSignalPtr = mEvaluationSignals.find( signalIDIn );
            if ( evaluationSignalPtr == mEvaluationSignals.end() )
            {
                return nullptr;
            }
            try
            {
                auto vecPtr = boost::get<SignalHistoryBuffer<T> *>( &( evaluationSignalPtr->second ) );
                if ( vecPtr != nullptr )
                {
                    return *vecPtr;
                }
            }
            catch ( ... )
            {
            }
            return nullptr;
        }

        template <typename T = double>
        FixedTimeWindowFunctionData<T> *
        getFixedTimeWindowFunctionDataPtr( InspectionSignalID signalIDIn )
        {
            auto evaluationfunctionPtr = mEvaluationFunctions.find( signalIDIn );
            if ( evaluationfunctionPtr == mEvaluationFunctions.end() )
            {
                return nullptr;
            }
            try
            {
                auto vecPtr = boost::get<FixedTimeWindowFunctionData<T> *>( &( evaluationfunctionPtr->second ) );
                if ( vecPtr != nullptr )
                {
                    return *vecPtr;
                }
            }
            catch ( ... )
            {
            }
            return nullptr;
        }
    };

    enum class ExpressionErrorCode
    {
        SUCCESSFUL,
        SIGNAL_NOT_FOUND,
        FUNCTION_DATA_NOT_AVAILABLE,
        STACK_DEPTH_REACHED,
        NOT_IMPLEMENTED_TYPE,
        NOT_IMPLEMENTED_FUNCTION
    };

    bool preAllocateBuffers();
    bool isSignalPartOfEval( const ExpressionNode *expression, InspectionSignalID signalID, int remainingStackDepth );

    ExpressionErrorCode eval( const ExpressionNode *expression,
                              ActiveCondition &condition,
                              InspectionValue &resultValueDouble,
                              bool &resultValueBool,
                              int remainingStackDepth );
    ExpressionErrorCode getLatestSignalValue( InspectionSignalID id,
                                              ActiveCondition &condition,
                                              InspectionValue &result );
    ExpressionErrorCode getSampleWindowFunction( WindowFunction function,
                                                 InspectionSignalID signalID,
                                                 ActiveCondition &condition,
                                                 InspectionValue &result );

    template <typename T>
    ExpressionErrorCode getSampleWindowFunctionType( WindowFunction function,
                                                     InspectionSignalID signalID,
                                                     ActiveCondition &condition,
                                                     InspectionValue &result );

    ExpressionErrorCode getGeohashFunctionNode( const ExpressionNode *expression,
                                                ActiveCondition &condition,
                                                bool &resultValueBool );
    template <typename T>
    void collectLastSignals( InspectionSignalID id,
                             uint32_t minimumSamplingInterval,
                             uint32_t maxNumberOfSignalsToCollect,
                             uint32_t conditionId,
                             SignalType signalTypeIn,
                             InspectionTimestamp &newestSignalTimestamp,
                             std::vector<CollectedSignal> &output );
    void collectLastCanFrames( CANRawFrameID canID,
                               CANChannelNumericID channelID,
                               uint32_t minimumSamplingInterval,
                               uint32_t maxNumberOfSignalsToCollect,
                               uint32_t conditionId,
                               InspectionTimestamp &newestSignalTimestamp,
                               std::vector<CollectedCanRawFrame> &output );

    template <typename T>
    void updateConditionBuffer( const InspectionMatrixSignalCollectionInfo &inspectionMatrixCollectionInfoIn,
                                ActiveCondition &acIn,
                                const long unsigned int conditionIndexIn );

    void updateAllFixedWindowFunctions( InspectionTimestamp timestamp );

    /**
     * @brief Generate a unique Identifier of an event. The event ID
     * consists of 4 bytes : 3 Bytes representing the lower bytes of the timestamp,
     * and 1 byte representing the event counter
     * @param timestamp in ms when the event occurred.
     * @return Unique Identifier of the event
     */
    static EventID generateEventID( InspectionTimestamp timestamp );

    void clear();

    /**
     * @brief Get an event counter since the last reset of the software.
     * @return One byte counter
     */
    static uint8_t
    generateEventCounter()
    {
        static std::atomic<uint8_t> counter( 0 );
        return ++counter;
    }

    // VSS supported datatypes
    using signalHistoryBufferVar = boost::variant<std::vector<SignalHistoryBuffer<uint8_t>>,
                                                  std::vector<SignalHistoryBuffer<int8_t>>,
                                                  std::vector<SignalHistoryBuffer<uint16_t>>,
                                                  std::vector<SignalHistoryBuffer<int16_t>>,
                                                  std::vector<SignalHistoryBuffer<uint32_t>>,
                                                  std::vector<SignalHistoryBuffer<int32_t>>,
                                                  std::vector<SignalHistoryBuffer<uint64_t>>,
                                                  std::vector<SignalHistoryBuffer<int64_t>>,
                                                  std::vector<SignalHistoryBuffer<float>>,
                                                  std::vector<SignalHistoryBuffer<double>>,
                                                  std::vector<SignalHistoryBuffer<bool>>>;
    using SignalHistoryBufferCollection = std::unordered_map<InspectionSignalID, signalHistoryBufferVar>;
    SignalHistoryBufferCollection
        mSignalBuffers; /**< signal history buffer. First vector has the signalID as index. In the nested vector
                         * the different subsampling of this signal are stored. */

    using SignalToBufferTypeMap = std::unordered_map<InspectionSignalID, SignalType>;
    SignalToBufferTypeMap mSignalToBufferTypeMap;

    template <typename T = double>
    std::vector<SignalHistoryBuffer<T>> *
    getSignalHistoryBufferPtr( InspectionSignalID signalIDIn )
    {
        std::vector<SignalHistoryBuffer<T>> *resVec = nullptr;
        if ( mSignalBuffers.find( signalIDIn ) == mSignalBuffers.end() )
        {
            // create a new map entry
            auto mapEntryVec = std::vector<SignalHistoryBuffer<T>>{};
            try
            {
                signalHistoryBufferVar mapEntry = mapEntryVec;
                mSignalBuffers.insert( { signalIDIn, mapEntry } );
            }
            catch ( ... )
            {
                FWE_LOG_ERROR( "Cannot Insert the signalHistoryBuffer vector for Signal " +
                               std::to_string( signalIDIn ) );
                return nullptr;
            }
        }

        try
        {
            auto signalBufferVectorPtr = mSignalBuffers.find( signalIDIn );
            if ( signalBufferVectorPtr != mSignalBuffers.end() )
            {
                resVec = boost::get<std::vector<SignalHistoryBuffer<T>>>( &( signalBufferVectorPtr->second ) );
            }
        }
        catch ( ... )
        {
            FWE_LOG_ERROR( "Cannot retrive the signalHistoryBuffer vector for Signal " + std::to_string( signalIDIn ) );
        }
        return resVec;
    }

    template <typename T>
    bool allocateBufferVector( SignalID signalIDIn, uint32_t &usedBytes );

    template <typename T = double>
    void updateBufferFixedWindowFunction( SignalID signalIDIn, InspectionTimestamp timestamp );

    template <typename T>
    ExpressionErrorCode getLatestBufferSignalValue( InspectionSignalID id,
                                                    ActiveCondition &condition,
                                                    InspectionValue &result );

    using CanFrameHistoryBufferCollection = std::vector<CanFrameHistoryBuffer>;
    CanFrameHistoryBufferCollection mCanFrameBuffers; /**< signal history buffer for raw can frames. */
    DTCInfo mActiveDTCs;
    SampleConsumed mActiveDTCsConsumed;

    bool
    isActiveDTCsConsumed( uint32_t conditionId )
    {
        return mActiveDTCsConsumed.isAlreadyConsumed( conditionId );
    }
    void
    setActiveDTCsConsumed( uint32_t conditionId, bool value )
    {
        mActiveDTCsConsumed.setAlreadyConsumed( conditionId, value );
    }

    GeohashFunctionNode mGeohashFunctionNode;
    // index in this bitset also the index in conditions vector.
    std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
        mConditionsWithInputSignalChanged; // bit is set if any signal or fixed window that this condition uses in its
                                           // condition changed
    std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
        mConditionsWithConditionCurrentlyTrue; // bit is set if the condition evaluated to true the last time
    std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
        mConditionsNotTriggeredWaitingPublished; // bit is set if condition is not triggered, if bit is not set it means
                                                 // condition is triggered and waits for its data to be sent out

    std::vector<ActiveCondition> mConditions;
    std::shared_ptr<const InspectionMatrix> mActiveInspectionMatrix;

    std::shared_ptr<const TriggeredCollectionSchemeData> collectData( ActiveCondition &condition,
                                                                      uint32_t conditionId,
                                                                      InspectionTimestamp &newestSignalTimestamp );
    uint32_t mNextConditionToCollectedIndex{ 0 };

    InspectionTimestamp mNextWindowFunctionTimesOut{ 0 };
    DataReduction mDataReduction;
    bool mSendDataOnlyOncePerCondition{ false };
};

template <typename T>
void
CollectionInspectionEngine::addNewSignal( InspectionSignalID id, const TimePoint &receiveTime, T value )
{
    if ( mSignalBuffers.find( id ) == mSignalBuffers.end() || mSignalBuffers[id].empty() )
    {
        // Signal not collected by any active condition
        return;
    }
    // Iterate through all sampling intervals of the signal
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( id );
    if ( signalHistoryBufferPtr == nullptr )
    {
        // Invalid access to the map Buffer datatype
        return;
    }
    auto &bufferVec = *signalHistoryBufferPtr;
    for ( auto &buf : bufferVec )
    {
        if ( ( ( buf.mSize > 0 ) && ( buf.mSize <= buf.mBuffer.size() ) ) &&
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

template <typename T>
bool
CollectionInspectionEngine::FixedTimeWindowFunctionData<T>::updateWindow(
    InspectionTimestamp timestamp, InspectionTimestamp &nextWindowFunctionTimesOut )
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
            mPreviousLastAvg = static_cast<T>( mCollectingSum / mCollectedSignals );
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
            mLastAvg = static_cast<T>( mCollectingSum / mCollectedSignals );
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

} // namespace IoTFleetWise
} // namespace Aws
