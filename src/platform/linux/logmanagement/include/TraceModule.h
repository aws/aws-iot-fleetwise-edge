// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "EnumUtility.h"
#include "Timer.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{
using namespace Aws::IoTFleetWise::Platform::Utility;
/**
 * Different Variables defined at compile time used by all other modules
 * For verbose print to work it needs to be also added to getVariableName()
 * */
enum class TraceVariable
{
    READ_SOCKET_FRAMES_0 = 0,
    READ_SOCKET_FRAMES_1,
    READ_SOCKET_FRAMES_2,
    READ_SOCKET_FRAMES_3,
    READ_SOCKET_FRAMES_4,
    READ_SOCKET_FRAMES_5,
    READ_SOCKET_FRAMES_6,
    READ_SOCKET_FRAMES_7,
    READ_SOCKET_FRAMES_8,
    READ_SOCKET_FRAMES_9,
    READ_SOCKET_FRAMES_10,
    READ_SOCKET_FRAMES_11,
    READ_SOCKET_FRAMES_12,
    READ_SOCKET_FRAMES_13,
    READ_SOCKET_FRAMES_14,
    READ_SOCKET_FRAMES_15,
    READ_SOCKET_FRAMES_16,
    READ_SOCKET_FRAMES_17,
    READ_SOCKET_FRAMES_18,
    READ_SOCKET_FRAMES_19, // If you add more, update references to this
    QUEUE_INSPECTION_TO_SENDER,
    MAX_SYSTEMTIME_KERNELTIME_DIFF,
    PM_MEMORY_NULL,
    PM_MEMORY_INSUFFICIENT,
    PM_COMPRESS_ERROR,
    PM_STORE_ERROR,
    CE_TOO_MANY_CONDITIONS,
    CE_SIGNAL_ID_OUTBOUND,
    CE_SAMPLE_SIZE_ZERO,
    GE_COMPARE_PRECISION_ERROR,
    GE_EVALUATE_ERROR_LAT_LON,
    OBD_VIN_ERROR,
    OBD_ENG_PID_REQ_ERROR,
    OBD_TRA_PID_REQ_ERROR,
    OBD_KEEP_ALIVE_ERROR,
    DISCARDED_FRAMES,
    CAN_POLLING_TIMESTAMP_COUNTER,
    CE_PROCESSED_SIGNALS,
    CE_PROCESSED_CAN_FRAMES,
    CE_TRIGGERS,
    OBD_POSSIBLE_PRECISION_LOSS_UINT64,
    OBD_POSSIBLE_PRECISION_LOSS_INT64,
    MQTT_SIGNAL_MESSAGES_SENT_OUT, // Can be multiple messages per event id
    MQTT_HEAP_USAGE,
    SIGNAL_BUFFER_SIZE,
    // If you add more, remember to add the name to TraceModule::getVariableName
    TRACE_VARIABLE_SIZE
};

/**
 *  Only add items at the end and do not delete items
 */
enum class TraceAtomicVariable
{
    QUEUE_CONSUMER_TO_INSPECTION_SIGNALS = 0,
    QUEUE_CONSUMER_TO_INSPECTION_CAN,
    NOT_TIME_MONOTONIC_FRAMES,
    SUBSCRIBE_ERROR,
    SUBSCRIBE_REJECT,
    CONNECTION_FAILED,
    CONNECTION_REJECTED,
    CONNECTION_INTERRUPTED,
    CONNECTION_RESUMED,
    COLLECTION_SCHEME_ERROR,
    // If you add more, remember to add the name to TraceModule::getAtomicVariableName
    TRACE_ATOMIC_VARIABLE_SIZE
};

/**
 * Different Sections defined at compile time used by all other modules
 * For verbose print to work it needs to be also added to getSectionName()
 * */
enum class TraceSection
{
    BUILD_MQTT = 0,
    FWE_STARTUP,
    FWE_SHUTDOWN,
    MANAGER_DECODER_BUILD,
    MANAGER_COLLECTION_BUILD,
    MANAGER_EXTRACTION,
    CAN_DECODER_CYCLE_0,
    CAN_DECODER_CYCLE_1,
    CAN_DECODER_CYCLE_2,
    CAN_DECODER_CYCLE_3,
    CAN_DECODER_CYCLE_4,
    CAN_DECODER_CYCLE_5,
    CAN_DECODER_CYCLE_6,
    CAN_DECODER_CYCLE_7,
    CAN_DECODER_CYCLE_8,
    CAN_DECODER_CYCLE_9,
    CAN_DECODER_CYCLE_10,
    CAN_DECODER_CYCLE_11,
    CAN_DECODER_CYCLE_12,
    CAN_DECODER_CYCLE_13,
    CAN_DECODER_CYCLE_14,
    CAN_DECODER_CYCLE_15,
    CAN_DECODER_CYCLE_16,
    CAN_DECODER_CYCLE_17,
    CAN_DECODER_CYCLE_18,
    CAN_DECODER_CYCLE_19, // If you add more, update references to this
    COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA,

    // If you add more, remember to add the name to TraceModule::getSectionName
    TRACE_SECTION_SIZE
};
/**
 * @brief An interface that can be implemented by different classes to store or upload metrics
 */
class IMetricsReceiver
{
public:
    virtual ~IMetricsReceiver() = default;

    /**
     * @brief set a value that will than be processed by the implementing class
     *
     * @param value the value as double
     * @param name the displayed name of the metric
     * @param unit the unit can be for example Seconds | Microseconds | Milliseconds | Bytes | Kilobytes | Megabytes |
     * Gigabytes | Terabytes | Bits | Kilobits | Megabits | Gigabits | Terabits | Percent | Count | Bytes/Second |
     * Kilobytes/Second | Megabytes/Second | Gigabytes/Second | Terabytes/Second | Bits/Second | Kilobits/Second |
     * Megabits/Second | Gigabits/Second | Terabits/Second | Count/Second | None
     */
    virtual void setMetric( const std::string &name, double value, const std::string &unit ) = 0;
};
/**
 * @brief Other modules can use TraceModule to trace variables or execution times for later analysis
 *
 * The class is a Singleton
 */
class TraceModule
{
public:
    /**
     * @brief Singleton implementation which is thread safe with C++11
     * */
    static TraceModule &
    get()
    {
        static TraceModule instance;
        return instance;
    }

    /**
     * @brief Set a variable defined in enum TraceVariable to trace its value
     * The inline call is fast and can be used everywhere. Eventually a cache miss
     * can occur.
     *
     * not thread safe for the same variable being set from different threads
     * @param variable the variable define in enum TraceVariable
     * @param value the uint64_t value that should be traced
     *
     */
    void
    setVariable( TraceVariable variable, uint64_t value )
    {
        auto index = toUType( variable );
        if ( ( variable < TraceVariable::TRACE_VARIABLE_SIZE ) && ( index >= 0 ) )
        {
            mVariableData[index].mCurrentValue = value;
            mVariableData[index].mMaxValue = std::max( value, mVariableData[index].mMaxValue );
        }
    }

    /**
     * @brief Add to a variable defined in enum TraceVariable to trace its value
     * The inline call is fast and can be used everywhere. Eventually a cache miss
     * can occur.
     *
     * not thread safe for the same variable being set from different threads
     * @param variable the variable define in enum TraceVariable
     * @param value the uint64_t value that should be added
     *
     */
    void
    addToVariable( TraceVariable variable, uint64_t value )
    {
        auto index = toUType( variable );
        if ( ( variable < TraceVariable::TRACE_VARIABLE_SIZE ) && ( index >= 0 ) )
        {
            setVariable( variable, mVariableData[index].mCurrentValue + value );
        }
    }

    /**
     * @brief Increment a variable defined in enum TraceVariable to trace its value
     * The inline call is fast and can be used everywhere. Eventually a cache miss
     * can occur.
     *
     * not thread safe for the same variable being set from different threads
     * @param variable the variable define in enum TraceVariable
     *
     */
    void
    incrementVariable( TraceVariable variable )
    {
        addToVariable( variable, 1 );
    }

    /**
     * @brief Add to a variable defined in enum TraceAtomicVariable to trace its value
     *
     * The value is initialized as 0 at the beginning. This function
     * can be called from different threads. Will do platform dependent atomic operations
     * used to implement std::atomic. So a few calls per microsecond should be fine
     * on most platforms.
     * There are no checks to avoid overflows/underflows.
     *
     * @param variable the variable define in enum TraceAtomicVariable
     * @param add the uint64_t value that should be added to the traced variable
     *
     */
    void
    addToAtomicVariable( TraceAtomicVariable variable, uint64_t add )
    {
        auto index = toUType( variable );
        if ( ( variable < TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ) && ( index >= 0 ) )
        {
            uint64_t currentValue = mAtomicVariableData[index].mCurrentValue.fetch_add( add );
            // If two threads add or increment in parallel the max value might be wrong
            mAtomicVariableData[index].mMaxValue = std::max( currentValue + add, mAtomicVariableData[index].mMaxValue );
        }
    }

    /**
     * @brief Increment a variable defined in enum TraceAtomicVariable by one to trace its value
     *
     * The value is initialized as 0 at the beginning. This function
     * can be called from different threads. Will do platform dependent atomic operations
     * used to implement std::atomic. So a few calls per microsecond should be fine
     * on most platforms.
     * There are no checks to avoid overflows/underflows.
     *
     * @param variable the variable define in enum TraceAtomicVariable
     *
     */
    void
    incrementAtomicVariable( TraceAtomicVariable variable )
    {
        addToAtomicVariable( variable, 1 );
    }

    /**
     * @brief Subtract a value from a variable defined in enum TraceAtomicVariable to trace its value
     *
     * The value is initialized as 0 at the beginning. This function
     * can be called from different threads. Will do platform dependent atomic operations
     * used to implement std::atomic. So a few calls per microsecond should be fine
     * on most platforms.
     * There are no checks to avoid overflows/underflows.
     *
     * @param variable the variable define in enum TraceAtomicVariable
     * @param sub the uint64_t value that should be subtracted to the traced variable
     *
     */
    void
    subtractFromAtomicVariable( TraceAtomicVariable variable, uint64_t sub )
    {
        auto index = toUType( variable );
        if ( ( variable < TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ) && ( index >= 0 ) )
        {
            mAtomicVariableData[index].mCurrentValue.fetch_sub( sub );
        }
    }

    /**
     * @brief Decrement a variable defined in enum TraceAtomicVariable by one to trace its value
     *
     * The value is initialized as 0 at the beginning. This function
     * can be called from different threads. Will do platform dependent atomic operations
     * used to implement std::atomic. So a few calls per microsecond should be fine
     * on most platforms.
     * There are no checks to avoid overflows/underflows.
     *
     * @param variable the variable define in enum TraceAtomicVariable
     *
     */
    void
    decrementAtomicVariable( TraceAtomicVariable variable )
    {
        subtractFromAtomicVariable( variable, 1 );
    }

    /**
     * @brief Get the maximum of a TraceVariable in the current observation window
     * @param variable the TraceVariable which content should be returned.
     *
     * @return the max value of the current observation window
     *
     */
    uint64_t
    getVariableMax( TraceVariable variable )
    {
        auto index = toUType( variable );
        if ( ( variable < TraceVariable::TRACE_VARIABLE_SIZE ) && ( index >= 0 ) )
        {
            return mVariableData[index].mMaxValue;
        }
        return 0;
    }

    /**
     * @brief Start a section which starts a timer until sectionEnd is called
     * The time between sectionBegin and sectionEnd will be traced. So the
     * max time the avg time of all executions of the same section will be
     * traced.
     * If the same TraceSection did already begin but not end the sectionBegin
     * preceding this sectionBegin will be ignored.
     * This is not thread safe so if the same section at the time begins or ends in two
     * threads at the same time data recorded might be wrong.
     *
     * @param section the section that begins after the call to sectionBegin
     *
     */
    void sectionBegin( TraceSection section );

    /**
     * @brief End a section which ends a timer started at the last sectionBegin
     * The time between sectionBegin and sectionEnd will be traced. So the
     * max time the avg time of all executions of the same section will be
     * traced.
     * If the same TraceSection did not begin yet or already ended this call will be
     * ignored.
     * This is not thread safe so if the same section at the time begins or ends in two
     * threads data recorded might be wrong.
     *
     * @param section the section that begins after the call to sectionBegin
     *
     */
    void sectionEnd( TraceSection section );

    /**
     * @brief Starts a new observation window for all variables and sections
     *
     * It might be interesting to collect for example the queue fill rate (traced
     * with the maximum of a TraceVariable) over certain time spans. For example
     * every second start a new observation window so you will get a trace giving the max
     * of a traced variable for every second. Additionally the overall max is still saved.
     * It is independent of the startNewObservationWindow so at any time you know what was
     * the maximum since starting FWE.
     * @param minimumObservationWindowTime if set higher than 0 the observation window will only be started if the
     * current observation window is longer than the value
     */
    void startNewObservationWindow( uint32_t minimumObservationWindowTime = 0 );

    /**
     * @brief prints all variables and section in a fixed format to stdout
     */
    void print();

    /**
     * @brief Calls for all variables and section the profiler->setMetric
     *
     * Any usage of the metrics that is not printing them to stdout should be implemented over
     * an IMetricsReceiver. The profiler pointer is not stored inside the class
     * @param profiler The instance that all data should be sent.
     */
    void forwardAllMetricsToMetricsReceiver( IMetricsReceiver *profiler );

private:
    static const char *getVariableName( TraceVariable variable );

    static const char *getAtomicVariableName( TraceAtomicVariable variable );

    static const char *getSectionName( TraceSection section );

    void updateAllTimeData();

    struct VariableData
    {
        uint64_t mCurrentValue;
        uint64_t mMaxValue; // The maximum in the current observation window
        uint64_t mMaxValueAllTime;
    };

    struct AtomicVariableData
    {
        std::atomic<uint64_t> mCurrentValue;
        uint64_t mMaxValue; // The maximum in the current observation window
        uint64_t mMaxValueAllTime;
    };

    struct SectionData
    {
        uint32_t mHitCounter;
        std::chrono::time_point<std::chrono::high_resolution_clock> mLastStartTime;
        std::chrono::time_point<std::chrono::high_resolution_clock> mLastEndTime;
        double mMaxSpent; // The maximum in the current observation window
        double mMaxSpentAllTime;
        double mMaxInterval; // The maximum in the current observation window
        double mMaxIntervalAllTime;
        double mTimeSpentSum;
        double mIntervalSum;
        bool mCurrentlyActive;
    };

    struct VariableData mVariableData[toUType( TraceVariable::TRACE_VARIABLE_SIZE )];

    struct AtomicVariableData mAtomicVariableData[toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE )];

    struct SectionData mSectionData[toUType( TraceSection::TRACE_SECTION_SIZE )];

    Timer mTimeSinceLastObservationWindow;
};
} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
