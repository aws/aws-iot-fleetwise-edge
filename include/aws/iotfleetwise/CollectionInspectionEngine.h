// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/EventTypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <algorithm>
#include <atomic>
#include <bitset> // As _Find_first() is not part of C++ standard and compiler specific other structure could be considered
#include <boost/core/demangle.hpp>
#include <boost/variant.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "aws/iotfleetwise/snf/StreamForwarder.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

static constexpr Timestamp MIN_FETCH_TRIGGER_MS = 1000;
// Rule A14-8-2 suggests to use class template specialization instead of function template specialization
template <typename T>
class NotifyRawBufferManager
{
public:
    static bool
    increaseElementUsage( SignalID id,
                          RawData::BufferManager *rawDataBufferManager,
                          RawData::BufferHandleUsageStage stage,
                          T value )
    {
        // For all not specialized types do nothing
        static_cast<void>( id );                   // Unused
        static_cast<void>( rawDataBufferManager ); // Unused
        static_cast<void>( value );                // Unused
        static_cast<void>( stage );                // Unused
        return false;
    }

    static bool
    decreaseElementUsage( SignalID id,
                          RawData::BufferManager *rawDataBufferManager,
                          RawData::BufferHandleUsageStage stage,
                          T value )
    {
        // For all not specialized types do nothing
        static_cast<void>( id );                   // Unused
        static_cast<void>( rawDataBufferManager ); // Unused
        static_cast<void>( value );                // Unused
        static_cast<void>( stage );                // Unused
        return false;
    }
};

template <>
class NotifyRawBufferManager<RawData::BufferHandle>
{
public:
    static bool
    increaseElementUsage( SignalID id,
                          RawData::BufferManager *rawDataBufferManager,
                          RawData::BufferHandleUsageStage stage,
                          RawData::BufferHandle value )
    {
        if ( rawDataBufferManager != nullptr )
        {
            rawDataBufferManager->increaseHandleUsageHint( id, value, stage );
            return true;
        }
        return false;
    }

    static bool
    decreaseElementUsage( SignalID id,
                          RawData::BufferManager *rawDataBufferManager,
                          RawData::BufferHandleUsageStage stage,
                          RawData::BufferHandle value )
    {
        if ( rawDataBufferManager != nullptr )
        {
            rawDataBufferManager->decreaseHandleUsageHint( id, value, stage );
            return true;
        }
        return false;
    }
};

/**
 * @brief Main class to implement collection and inspection engine logic
 *
 * This class is not multithreading safe to the caller needs to ensure that the different functions
 * are called only from one thread. This class will be instantiated and used from the Collection
 * Inspection Engine thread
 */
class CollectionInspectionEngine
{

public:
    CollectionInspectionEngine(
        RawData::BufferManager *rawDataBufferManager, /**< the buffer manager to use for the raw data buffer */
        uint32_t minFetchTriggerIntervalMs =
            MIN_FETCH_TRIGGER_MS, /**< the minimum interval in milliseconds between two fetch triggers */
        std::shared_ptr<FetchRequestQueue> fetchQueue = nullptr, /**< fetch requests will be pushed to this queue and
                                                                    executed by fetch manager when condition is true */
        bool sendDataOnlyOncePerCondition = true /**< if true only data with a millisecond timestamp, bigger than the
                                                    timestamp the condition last sent out data, will be included */
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        ,
        Aws::IoTFleetWise::Store::StreamForwarder *streamForwarder = nullptr /**< Store and forward stream forwarder */
#endif
    );

    void onChangeInspectionMatrix( std::shared_ptr<const InspectionMatrix> inspectionMatrix,
                                   const TimePoint &currentTime );

    /**
     * @brief Go through all conditions incl. fetch conditions with changed condition signals and evaluate condition
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
    CollectionInspectionEngineOutput collectNextDataToSend( const TimePoint &currentTime, uint32_t &waitTimeMs );

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
     * @param fetchRequestID fetch request ID that signal was collected for
     * @param receiveTime timestamp at which time was the signal seen on the physical bus
     * @param currentMonotonicTimeMs current monotonic time for window function evaluation
     * @param value the signal value
     */
    template <typename T>
    void addNewSignal( SignalID id,
                       FetchRequestID fetchRequestID,
                       const TimePoint &receiveTime,
                       const Timestamp &currentMonotonicTimeMs,
                       T value );

    /**
     * @brief Add new signal buffer entry to mSignalBuffers (SignalHistoryBufferCollection)
     *
     * There is one buffer per sampling interval. For each sampling interval buffer us resized to the biggest requested
     * size.
     */
    template <typename T>
    void addSignalBuffer( const InspectionMatrixSignalCollectionInfo &signal,
                          SignalBufferConditionID signalBufferConditionIndex );

    void setActiveDTCs( const DTCInfo &activeDTCs );

    void registerCustomFunction( const std::string &name, CustomFunctionCallbacks callbacks );

private:
    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
    static const uint32_t MAX_SAMPLE_MEMORY = 20 * 1024 * 1024; // 20MB max for all samples
    uint32_t mMinFetchTriggerIntervalMs;
    std::shared_ptr<FetchRequestQueue> mFetchQueue;

    static inline double
    EVAL_EQUAL_DISTANCE()
    {
        return 0.001;
    } // because static const double (non-integral type) not possible

    /**
     * @brief maintains values like avg, min or max calculated over a certain time window
     *
     * All values here are calculated with an online algorithm, so everytime
     * there is a new value for a signal all internal values are updated. There is no
     * need to look at historic values. The window is time based and not sample based.
     * Currently the last 2 windows are maintained inside this class.
     * */
    template <typename T>
    class FixedTimeWindowFunctionData
    {
    public:
        FixedTimeWindowFunctionData( uint32_t size )
            : mWindowSizeMs( size )
        {
        }

        Timestamp mWindowSizeMs{ 0 };       /** <over which time is the window calculated */
        Timestamp mLastTimeCalculated{ 0 }; /** <the time the last window stopped*/

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
        bool updateWindow( Timestamp timestamp, Timestamp &nextWindowFunctionTimesOut );
        inline bool
        addValue( T value, Timestamp timestamp, Timestamp &nextWindowFunctionTimesOut )
        {
            updateInternalVariables( value );
            return updateWindow( timestamp, nextWindowFunctionTimesOut );
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
        initNewWindow( Timestamp timestamp, Timestamp &nextWindowFunctionTimesOut )
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
    template <typename T>
    struct SignalHistoryBuffer
    {
        SignalHistoryBuffer( uint32_t size, uint32_t sampleInterval, bool containsRawDataHandles = false )
            : mMinimumSampleIntervalMs( sampleInterval )
            , mSize( size )
            , mCurrentPosition( mSize - 1 )
            , mContainsRawDataHandles( containsRawDataHandles )
        {
        }

        uint32_t mMinimumSampleIntervalMs{ 0 };
        std::vector<struct SignalSample<T>>
            mBuffer; // ringbuffer, Consider to move to raw pointer allocated with new[] if vector allocates too much
        size_t mSize{ 0 };            // minimum size needed by all conditions, buffer must be at least this big
        size_t mCurrentPosition{ 0 }; /**< position in ringbuffer needs to come after size as it depends on it */
        size_t mCounter{ 0 };         /**< over all recorded samples*/
        TimePoint mLastSample{ 0, 0 };
        bool mContainsRawDataHandles{ false };
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

    // VSS supported datatypes
    using SignalHistoryBufferPtrVar = boost::variant<SignalHistoryBuffer<int64_t> *,
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
        Timestamp mLastDataTimestampPublished{ 0 };
        TimePoint mLastTrigger{ 0, 0 };
        std::unordered_map<SignalID, SignalHistoryBufferPtrVar>
            mConditionSignals; // for fast lookup signals used for evaluation or collection
        std::unordered_map<SignalID, FixedTimeWindowFunctionPtrVar>
            mEvaluationFunctions; // for fast lookup functions used for evaluation
        const ConditionWithCollectedData &mCondition;
        // Unique Identifier of the Event matched by this condition.
        EventID mEventID{ 0 };
        std::unordered_set<SignalID> mCollectedSignalIds;
        CollectionInspectionEngineOutput mCollectedData;

        template <typename T>
        SignalHistoryBuffer<T> *
        getEvaluationSignalsBufferPtr( SignalID signalID )
        {
            auto evaluationSignalPtr = mConditionSignals.find( signalID );
            if ( evaluationSignalPtr == mConditionSignals.end() )
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

        template <typename T>
        FixedTimeWindowFunctionData<T> *
        getFixedTimeWindowFunctionDataPtr( SignalID signalID )
        {
            auto evaluationfunctionPtr = mEvaluationFunctions.find( signalID );
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

    bool preAllocateBuffers();

    ExpressionErrorCode eval( const ExpressionNode *expression,
                              ActiveCondition &condition,
                              InspectionValue &resultValue,
                              int remainingStackDepth,
                              uint32_t conditionId );

    /**
     * @brief Evaluate static conditions once when new inspection matrix arrives
     */
    void evaluateStaticCondition( uint32_t conditionIndex );

    ExpressionErrorCode getLatestSignalValue( SignalID id, ActiveCondition &condition, InspectionValue &result );
    ExpressionErrorCode getSampleWindowFunction( WindowFunction function,
                                                 SignalID signalID,
                                                 ActiveCondition &condition,
                                                 InspectionValue &result );

    template <typename T>
    ExpressionErrorCode getSampleWindowFunctionType( WindowFunction function,
                                                     SignalID signalID,
                                                     ActiveCondition &condition,
                                                     InspectionValue &result );

    template <typename T>
    void collectLastSignals( SignalID id,
                             size_t maxNumberOfSignalsToCollect,
                             uint32_t conditionId,
                             SignalType signalType,
                             Timestamp &newestSignalTimestamp,
                             std::vector<CollectedSignal> &output );

    template <typename T>
    void updateConditionBuffer( const InspectionMatrixSignalCollectionInfo &inspectionMatrixCollectionInfo,
                                ActiveCondition &activeCondition,
                                const long unsigned int conditionIndex );

    void updateAllFixedWindowFunctions( Timestamp timestamp );

    /**
     * @brief Generate a unique Identifier of an event. The event ID
     * consists of 4 bytes : 3 Bytes representing the lower bytes of the timestamp,
     * and 1 byte representing the event counter
     * @param timestamp in ms when the event occurred.
     * @return Unique Identifier of the event
     */
    static EventID generateEventID( Timestamp timestamp );

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

    ExpressionErrorCode isNewSignalValueAvailable( SignalID signalID,
                                                   ActiveCondition &condition,
                                                   InspectionValue &resultValue,
                                                   uint32_t conditionId );
    template <typename T>
    ExpressionErrorCode isNewSignalValueAvailableType( SignalID signalID,
                                                       ActiveCondition &condition,
                                                       InspectionValue &resultValue,
                                                       uint32_t conditionId );

    // VSS supported datatypes
    using SignalHistoryBuffersVar = boost::variant<std::vector<SignalHistoryBuffer<uint8_t>>,
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
    using SignalHistoryBufferCollection =
        std::unordered_map<SignalBufferConditionID, std::unordered_map<SignalID, SignalHistoryBuffersVar>>;
    SignalHistoryBufferCollection
        mSignalBuffers; /**< signal history buffer. Vector has the "virtual" index, that is based of
                         * condition index and fetch request id, and signalID as index. In the nested vector
                         * the different subsampling of this signal are stored. */

    using FetchRequestToConditionIndexMap = std::unordered_map<FetchRequestID, SignalBufferConditionID>;
    FetchRequestToConditionIndexMap mFetchRequestToConditionIndexMap;

    using SignalToBufferTypeMap = std::unordered_map<SignalID, SignalType>;
    SignalToBufferTypeMap mSignalToBufferTypeMap;

    /**
     * @brief This function will either return existing signal buffer vector or create a new one
     */
    template <typename T>
    std::vector<SignalHistoryBuffer<T>> *
    getSignalHistoryBuffersPtr( SignalID signalID, SignalBufferConditionID signalBufferConditionIndex )
    {
        std::vector<SignalHistoryBuffer<T>> *resVec = nullptr;

        auto outerMapIt = mSignalBuffers.find( signalBufferConditionIndex );
        if ( outerMapIt == mSignalBuffers.end() )
        {
            outerMapIt = mSignalBuffers.insert( { signalBufferConditionIndex, {} } ).first;
        }
        auto signalBufferVectorPtr = outerMapIt->second.find( signalID );

        if ( signalBufferVectorPtr == outerMapIt->second.end() )
        {
            // create a new map entry
            auto mapEntryVec = std::vector<SignalHistoryBuffer<T>>{};
            try
            {
                SignalHistoryBuffersVar mapEntry = mapEntryVec;
                FWE_LOG_TRACE( "Creating new signalHistoryBuffer vector for Signal " + std::to_string( signalID ) +
                               " with type " + boost::core::demangle( typeid( T ).name() ) );
                signalBufferVectorPtr = outerMapIt->second.insert( { signalID, mapEntry } ).first;
            }
            catch ( ... )
            {
                FWE_LOG_ERROR( "Cannot Insert the signalHistoryBuffer vector for Signal " +
                               std::to_string( signalID ) );
                return nullptr;
            }
        }

        try
        {
            resVec = boost::get<std::vector<SignalHistoryBuffer<T>>>( &( signalBufferVectorPtr->second ) );
            if ( resVec == nullptr )
            {
                FWE_LOG_ERROR( "Could not retrieve the signalHistoryBuffer vector for Signal " +
                               std::to_string( signalID ) +
                               ". Tried with type: " + boost::core::demangle( typeid( T ).name() ) +
                               ". The buffer was likely created with the wrong type." );
            }
        }
        catch ( ... )
        {
            FWE_LOG_ERROR( "Cannot retrieve the signalHistoryBuffer vector for Signal " + std::to_string( signalID ) );
        }
        return resVec;
    }

    /**
     * @brief This function will return existing signal buffer for given signal id and sample rate
     */
    template <typename T>
    SignalHistoryBuffer<T> *
    getSignalHistoryBufferPtr( SignalID signalID,
                               SignalBufferConditionID signalBufferConditionIndex,
                               uint32_t minimumSampleIntervalMs )
    {
        auto signalHistoryBufferVectorPtr = getSignalHistoryBuffersPtr<T>( signalID, signalBufferConditionIndex );
        if ( signalHistoryBufferVectorPtr != nullptr )
        {
            for ( auto &buffer : *signalHistoryBufferVectorPtr )
            {
                if ( buffer.mMinimumSampleIntervalMs == minimumSampleIntervalMs )
                {
                    return &buffer;
                }
            }
        }
        return nullptr;
    }

    template <typename T>
    bool allocateBufferVector( SignalID signalID,
                               SignalBufferConditionID signalBufferConditionIndex,
                               size_t &usedBytes );

    template <typename T>
    void updateBufferFixedWindowFunction( SignalID signalID,
                                          SignalBufferConditionID signalBufferConditionIndex,
                                          Timestamp timestamp );

    template <typename T>
    ExpressionErrorCode getLatestBufferSignalValue( SignalID id, ActiveCondition &condition, InspectionValue &result );

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

    // index in this bitset also the index in conditions vector.
    std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
        mConditionsWithInputSignalChanged; // bit is set if any signal or fixed window that this condition uses in its
                                           // condition changed
    std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
        mConditionsWithConditionCurrentlyTrue; // bit is set if the condition evaluated to true the last time
    std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
        mConditionsTriggeredWaitingPublished; // bit is set if condition is triggered and waits for its data to be sent
                                              // out, bit is not set if condition is not triggered

    std::bitset<MAX_NUMBER_OF_ACTIVE_FETCH_CONDITION>
        mFetchConditionsWithConditionCurrentlyTrue; // bit is set if the fetch condition evaluated to true the last time
                                                    // indexed by fetch request ID which is unique and generated on
                                                    // scheme arrival

    std::vector<ActiveCondition> mConditions;
    std::shared_ptr<const InspectionMatrix> mActiveInspectionMatrix;

    void collectData( ActiveCondition &condition,
                      uint32_t conditionId,
                      Timestamp &newestSignalTimestamp,
                      CollectionInspectionEngineOutput &output );
    uint32_t mNextConditionToCollectedIndex{ 0 };

    Timestamp mNextWindowFunctionTimesOut{ 0 };
    bool mSendDataOnlyOncePerCondition{ false };

    std::unordered_map<FetchRequestID, Timestamp> mLastFetchTrigger;
    RawData::BufferManager *mRawDataBufferManager{ nullptr };
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    Aws::IoTFleetWise::Store::StreamForwarder *mStreamForwarder;
#endif

    std::unordered_map<std::string, CustomFunctionCallbacks> mCustomFunctionCallbacks;
    void cleanupCustomFunctions( const ExpressionNode *expression );
};

template <typename T>
void
CollectionInspectionEngine::addNewSignal( SignalID id,
                                          FetchRequestID fetchRequestID,
                                          const TimePoint &receiveTime,
                                          const Timestamp &currentMonotonicTimeMs,
                                          T value )
{
    auto signalBufferConditionIndex = mFetchRequestToConditionIndexMap[fetchRequestID];
    auto outerSignalBufferMapIt = mSignalBuffers.find( signalBufferConditionIndex );

    if ( outerSignalBufferMapIt == mSignalBuffers.end() ||
         outerSignalBufferMapIt->second.find( id ) == outerSignalBufferMapIt->second.end() )
    {
        // Signal not collected by any active condition
        return;
    }
    // Iterate through all sampling intervals of the signal
    std::vector<SignalHistoryBuffer<T>> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBuffersPtr<T>( id, signalBufferConditionIndex );
    if ( signalHistoryBufferPtr == nullptr )
    {
        // Invalid access to the map Buffer datatype
        return;
    }
    auto &bufferVec = *signalHistoryBufferPtr;
    for ( auto &buf : bufferVec )
    {
        if ( ( ( buf.mSize > 0 ) && ( buf.mSize <= buf.mBuffer.size() ) ) && // buffer isn't full
             ( ( buf.mMinimumSampleIntervalMs == 0 ) || // sample rate is 0 = all incoming signals are collected
               ( ( buf.mLastSample.systemTimeMs == 0 ) &&
                 ( buf.mLastSample.monotonicTimeMs == 0 ) ) || // first sample that is collected
               ( receiveTime.monotonicTimeMs >=
                 buf.mLastSample.monotonicTimeMs + buf.mMinimumSampleIntervalMs ) ) ) // sample time has passed
        {
            auto oldValue = buf.mBuffer[buf.mCurrentPosition].mValue;
            buf.mCurrentPosition++;
            if ( buf.mCurrentPosition >= buf.mSize )
            {
                buf.mCurrentPosition = 0;
            }
            if ( buf.mContainsRawDataHandles && ( buf.mCounter >= buf.mSize ) )
            {
                // release data that is going to be overwritten
                NotifyRawBufferManager<T>::decreaseElementUsage(
                    id,
                    mRawDataBufferManager,
                    RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER,
                    buf.mBuffer[buf.mCurrentPosition].mValue );
            }
            // insert value to the buffer
            buf.mBuffer[buf.mCurrentPosition].mValue = value;
            buf.mBuffer[buf.mCurrentPosition].mTimestamp = receiveTime.systemTimeMs;
            buf.mBuffer[buf.mCurrentPosition].setAlreadyConsumed( ALL_CONDITIONS, false );
            buf.mCounter++;
            buf.mLastSample = receiveTime;
            for ( auto &window : buf.mWindowFunctionData )
            {
                if ( window.addValue( value, currentMonotonicTimeMs, mNextWindowFunctionTimesOut ) )
                {
                    // Window function values were recalculated
                    mConditionsWithInputSignalChanged |= buf.mConditionsThatEvaluateOnThisSignal;
                }
            }
            if ( oldValue != value )
            {
                mConditionsWithInputSignalChanged |= buf.mConditionsThatEvaluateOnThisSignal;
            }
            if ( buf.mContainsRawDataHandles )
            {
                NotifyRawBufferManager<T>::increaseElementUsage(
                    id,
                    mRawDataBufferManager,
                    RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER,
                    value );
            }
        }
    }
}

template <typename T>
bool
CollectionInspectionEngine::FixedTimeWindowFunctionData<T>::updateWindow( Timestamp timestamp,
                                                                          Timestamp &nextWindowFunctionTimesOut )
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
