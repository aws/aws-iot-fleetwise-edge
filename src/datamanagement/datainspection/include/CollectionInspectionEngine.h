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

#pragma once

#include "DataReduction.h"
#include "GeohashFunctionNode.h"
#include "IActiveConditionProcessor.h"
#include "InspectionEventListener.h"
#include "Listener.h"
#include "LoggingModule.h"
#include <limits>
#include <unordered_map>
// As _Find_first() is not part of C++ standard and compiler specific other structure could be considered
#include <bitset>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform::Linux;
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
    using InspectionSignalID = uint32_t;
    using InspectionValue = double;

    /**
     * @brief Construct the CollectionInspectionEngine which handles multiple conditions
     *
     * @param sendDataOnlyOncePerCondition if true only data with a millisecond timestamp, bigger than the timestamp the
     * condition last sent out data, will be included.
     */
    CollectionInspectionEngine( bool sendDataOnlyOncePerCondition = true );

    void onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &activeInspectionMatrix ) override;

    /**
     * @brief Go through all conditions with changed condition signals and evaluate condition
     *
     * This needs to be called directly after new signals are added to the CollectionEngine.
     * If multiple samples of the same signal are added to CollectionEngine without calling
     * this function data can get lost and triggers can get missed.
     * @return at least one condition is true
     *
     */
    bool evaluateConditions( InspectionTimestamp currentTime );

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
    std::shared_ptr<const TriggeredCollectionSchemeData> collectNextDataToSend( InspectionTimestamp currentTime,
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
     * @param value the signal value as double
     */
    void addNewSignal( InspectionSignalID id, InspectionTimestamp receiveTime, InspectionValue value );

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
                            InspectionTimestamp receiveTime,
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
    struct SignalSample : SampleConsumed
    {
        InspectionValue mValue{ 0.0 };
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
        InspectionValue mLastMin{ std::numeric_limits<InspectionValue>::min() };
        InspectionValue mLastMax{ std::numeric_limits<InspectionValue>::max() };
        InspectionValue mLastAvg{ 0 };
        bool mLastAvailable{ false }; /** <if this is false there is no data for this window. For example if the window
                                       * is 30 minutes long in the first 30 minutes of running FWE receiving the signal
                                       * no data is available
                                       */
        // This values represents the value before the newest window
        InspectionValue mPreviousLastMin{ std::numeric_limits<InspectionValue>::min() };
        InspectionValue mPreviousLastMax{ std::numeric_limits<InspectionValue>::max() };
        InspectionValue mPreviousLastAvg{ 0 };
        bool mPreviousLastAvailable{ false };

        // This values are changed online with every new signal sample and will be used to calculate
        // the next window as soon as the window time is over
        InspectionValue mCollectingMin{ std::numeric_limits<InspectionValue>::min() };
        InspectionValue mCollectingMax{ std::numeric_limits<InspectionValue>::max() };
        InspectionValue mCollectingSum{ 0 };
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
        addValue( InspectionValue value,
                  InspectionTimestamp timestamp,
                  InspectionTimestamp &nextWindowFunctionTimesOut )
        {
            updateWindow( timestamp, nextWindowFunctionTimesOut );
            updateInternalVariables( value );
        }

    private:
        inline void
        updateInternalVariables( InspectionValue value )
        {
            mCollectingMin = std::min( mCollectingMin, value );
            mCollectingMax = std::max( mCollectingMax, value );
            mCollectingSum += value;
            mCollectedSignals++;
        }
        inline void
        initNewWindow( InspectionTimestamp timestamp, InspectionTimestamp &nextWindowFunctionTimesOut )
        {
            mCollectingMin = std::numeric_limits<InspectionValue>::max();
            mCollectingMax = std::numeric_limits<InspectionValue>::min();
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
    struct SignalHistoryBuffer
    {
        SignalHistoryBuffer() = default;
        SignalHistoryBuffer( uint32_t sizeIn, uint32_t sampleInterval )
            : mMinimumSampleIntervalMs( sampleInterval )
            , mSize( sizeIn )
            , mCurrentPosition( mSize - 1 )
        {
        }

        uint32_t mMinimumSampleIntervalMs{ 0 };
        std::vector<struct SignalSample>
            mBuffer; // ringbuffer, Consider to move to raw pointer allocated with new[] if vector allocates too much
        uint32_t mSize{ 0 };            // minimum size needed by all conditions, buffer must be at least this big
        uint32_t mCurrentPosition{ 0 }; /**< position in ringbuffer needs to come after size as it depends on it */
        uint32_t mCounter{ 0 };         /**< over all recorded samples*/
        InspectionTimestamp mLastSample{ 0 };
        std::vector<FixedTimeWindowFunctionData>
            mWindowFunctionData; /**< every signal buffer can have multiple windows over different time periods*/
        std::bitset<MAX_NUMBER_OF_ACTIVE_CONDITION>
            mConditionsThatEvaluateOnThisSignal; /**< if bit 0 is set it means element with index 0 of vector conditions
                                                 needs to reevaluate if this signal changes*/

        inline FixedTimeWindowFunctionData *
        addFixedWindow( uint32_t windowSizeMs )
        {
            if ( windowSizeMs == 0 )
            {
                return nullptr;
            }
            FixedTimeWindowFunctionData *findWindow = getFixedWindow( windowSizeMs );
            if ( findWindow != nullptr )
            {
                return findWindow;
            }
            mWindowFunctionData.emplace_back( windowSizeMs );
            return &( mWindowFunctionData.back() );
        }

        inline FixedTimeWindowFunctionData *
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
        InspectionTimestamp mLastSample{ 0 };
    };

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
        InspectionTimestamp mLastTrigger{ 0 };
        std::unordered_map<InspectionSignalID, SignalHistoryBuffer *>
            mEvaluationSignals; // for fast lookup signals used for evaluation
        std::unordered_map<InspectionSignalID, FixedTimeWindowFunctionData *>
            mEvaluationFunctions; // for fast lookup functions used for evaluation
        const ConditionWithCollectedData &mCondition;
        // Unique Identifier of the Event matched by this condition.
        EventID mEventID{ 0 };
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

    SignalHistoryBuffer &addSignalToBuffer( const InspectionMatrixSignalCollectionInfo &signal );
    bool preAllocateBuffers();
    bool isSignalPartOfEval( const struct ExpressionNode *expression,
                             InspectionSignalID signalID,
                             int remainingStackDepth );

    ExpressionErrorCode eval( const struct ExpressionNode *expression,
                              ActiveCondition &condition,
                              InspectionValue &resultValueDouble,
                              bool &resultValueBool,
                              int remainingStackDepth );
    ExpressionErrorCode getLatestSignalValue( InspectionSignalID id,
                                              ActiveCondition &condition,
                                              InspectionValue &result );
    static ExpressionErrorCode getSampleWindowFunction( WindowFunction function,
                                                        InspectionSignalID signalID,
                                                        ActiveCondition &condition,
                                                        InspectionValue &result );
    ExpressionErrorCode getGeohashFunctionNode( const struct ExpressionNode *expression,
                                                ActiveCondition &condition,
                                                bool &resultValueBool );
    void collectLastSignals( InspectionSignalID id,
                             uint32_t minimumSamplingInterval,
                             uint32_t maxNumberOfSignalsToCollect,
                             uint32_t conditionId,
                             InspectionTimestamp &newestSignalTimestamp,
                             std::vector<CollectedSignal> &output );
    void collectLastCanFrames( CANRawFrameID canID,
                               CANChannelNumericID channelID,
                               uint32_t minimumSamplingInterval,
                               uint32_t maxNumberOfSignalsToCollect,
                               uint32_t conditionId,
                               InspectionTimestamp &newestSignalTimestamp,
                               std::vector<CollectedCanRawFrame> &output );

    void updateAllFixedWindowFunctions( InspectionTimestamp timestamp );

    /**
     * @brief Generate a unique Identifier of an event. The event ID
     * consists of 4 bytes : 3 Bytes representing the lower bytes of the timestamp,
     * and 1 byte representing the event counter
     * @param timestamp in ms when the event occurred.
     * @return Unique Identifier of the event
     */
    static EventID generateEventID( InspectionTimestamp timestamp );

    /**
     * @brief This function looks the the active condition and
     * checks whether other sensors such as Camera needs to be requested for
     * extra metadata. It does then notify if that's the case.
     * @param condition current condition being inspected.
     */
    void evaluateAndTriggerRichSensorCapture( const ActiveCondition &condition );

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

    using SignalHistoryBufferCollection = std::unordered_map<InspectionSignalID, std::vector<SignalHistoryBuffer>>;
    SignalHistoryBufferCollection
        mSignalBuffers; /**< signal history buffer. First vector has the signalID as index. In the nested vector
                         * the different subsampling of this signal are stored. */

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
    Aws::IoTFleetWise::Platform::Linux::LoggingModule mLogger;
    DataReduction mDataReduction;
    bool mSendDataOnlyOncePerCondition{ false };
};

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
