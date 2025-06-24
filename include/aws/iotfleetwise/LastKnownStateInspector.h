// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CommandTypes.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/LastKnownStateTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <boost/variant.hpp>
#include <cstdint>
#include <cstdlib>
#include <json/json.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Main class to implement last known state inspection engine logic
 *
 * This class is not thread-safe. The caller needs to ensure that the different functions
 * are called only from one thread. This class will be instantiated and used from the Last Known
 * State Inspector thread
 */
class LastKnownStateInspector
{

public:
    LastKnownStateInspector( std::shared_ptr<DataSenderQueue> commandResponses,
                             std::shared_ptr<CacheAndPersist> schemaPersistency );

    /**
     * @brief This function handles when there's an update on LKS inspection matrix
     *
     * @param stateTemplates List containing all state templates
     *
     */
    void onStateTemplatesChanged( const StateTemplateList &stateTemplates );

    /**
     * @brief Handle a new LastKnownState command
     */
    void onNewCommandReceived( const LastKnownStateCommandRequest &lastKnownStateCommandRequest );

    /**
     * @brief Copy for a triggered condition data out of the signal buffer
     *
     * It will copy the data the next triggered condition wants to publish out of
     * the signal history buffer.
     * It will allocate new memory for the data and return a shared ptr to it.
     * This data can then be passed on to be serialized and sent to the cloud.
     * Should be called after dataReadyToBeSent() true an shortly after adding new signals
     *
     * @param currentTime current time for send data out
     *
     * @return the collected data, or nullptr
     */
    std::shared_ptr<const LastKnownStateCollectedData> collectNextDataToSend( const TimePoint &currentTime );
    /**
     * @brief Inspect new sample of signal and also cache it
     *
     * The signals should come in ordered by time (oldest signals first)
     *
     * @param id signal ID
     * @param receiveTime timestamp at which time was the signal seen on the physical bus
     * @param value the signal value
     */
    template <typename T>
    void inspectNewSignal( SignalID id, const TimePoint &receiveTime, T value );

private:
    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
    static const uint32_t MAX_SAMPLE_MEMORY = 20 * 1024 * 1024; // 20MB max for all samples
    // For Last Known State, we set the signal history buffer sample size as 1.
    // coverity[autosar_cpp14_a0_1_1_violation:FALSE] variable is used
    static const uint32_t MAX_SIGNAL_HISTORY_BUFFER_SIZE = 1;
    // coverity[autosar_cpp14_a0_1_3_violation] EVAL_EQUAL_DISTANCE is used in template function below
    static inline double
    EVAL_EQUAL_DISTANCE()
    {
        return 0.001;
    } // because static const double (non-integral type) not possible

    /**
     * @brief stores the history of one signal.
     *
     * The signal can be used as part of a condition or only to be published in the case
     * a condition is true
     */
    template <typename T = double>
    struct SignalHistoryBuffer
    {
        // no lint for using equals default constructor as there's an open bug on compiler
        // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=88165
        // NOLINTNEXTLINE(modernize-use-equals-default)
        SignalHistoryBuffer()
        {
        }
        std::vector<struct SignalSample<T>>
            mBuffer; // ringbuffer, Consider to move to raw pointer allocated with new[] if vector allocates too much
        size_t mSize{ 0 };              // minimum size needed by all conditions, buffer must be at least this big
        uint32_t mCurrentPosition{ 0 }; /**< position in ringbuffer needs to come after size as it depends on it */
        uint32_t mCounter{ 0 };         /**< over all recorded samples*/
        TimePoint mLastSample{ 0, 0 };
    };

    /**
     * @brief This function allocate signal history buffer for the signal
     */
    template <typename T = double>
    void addSignalBuffer( const LastKnownStateSignalInformation &signalIn );

    /**
     * @brief Allocate memory for signal history buffer
     * @param signalID signal ID
     * @param usedBytes total allocated bytes for vector mBuffer in signal history buffer.
     * @param signalSampleSize number of bytes for the instantiated template
     */
    template <typename T>
    bool allocateBuffer( SignalID signalID, size_t &usedBytes, size_t signalSampleSize );

    /**
     * @brief Allocate memory for signal history buffer
     */
    bool preAllocateBuffers();

    /**
     * @brief This function signals that are ready for update
     * @param collectedSignals where the collected signals should be added to
     * @param signalID signal ID
     * @param lastTriggerTime last trigger time of timestamp condition (system time)
     */
    void collectData( std::vector<CollectedSignal> &collectedSignals, SignalID signalID, Timestamp lastTriggerTime );

    /**
     * @brief This function collect the latest sample of signals
     * @param collectedSignals where the collected signals should be added to
     * @param signalID signal ID
     * @param lastTriggerTime last trigger time of timestamp condition (system time)
     */
    template <typename T>
    void collectLatestSignal( std::vector<CollectedSignal> &collectedSignals,
                              SignalID signalID,
                              Timestamp lastTriggerTime );

    /**
     * @brief clear the inspection logic as well as buffered data for state templates that were removed
     *
     * @param newStateTemplates List containing all state templates. Anything not included in this list will be removed
     */
    void clearUnused( const StateTemplateList &newStateTemplates );

    // VSS supported datatypes
    using signalHistoryBufferVar = boost::variant<SignalHistoryBuffer<uint8_t>,
                                                  SignalHistoryBuffer<int8_t>,
                                                  SignalHistoryBuffer<uint16_t>,
                                                  SignalHistoryBuffer<int16_t>,
                                                  SignalHistoryBuffer<uint32_t>,
                                                  SignalHistoryBuffer<int32_t>,
                                                  SignalHistoryBuffer<uint64_t>,
                                                  SignalHistoryBuffer<int64_t>,
                                                  SignalHistoryBuffer<float>,
                                                  SignalHistoryBuffer<double>,
                                                  SignalHistoryBuffer<bool>>;
    using SignalHistoryBufferCollection = std::unordered_map<SignalID, signalHistoryBufferVar>;
    SignalHistoryBufferCollection
        mSignalBuffers; /**< signal history buffer. First vector has the signalID as index. In the nested vector
                         * the different subsampling of this signal are stored. */

    using SignalToBufferTypeMap = std::unordered_map<SignalID, SignalType>;
    SignalToBufferTypeMap mSignalToBufferTypeMap;

    template <typename T = double>
    SignalHistoryBuffer<T> *
    getSignalHistoryBufferPtr( SignalID signalID )
    {
        SignalHistoryBuffer<T> *resVec = nullptr;
        if ( mSignalBuffers.find( signalID ) == mSignalBuffers.end() )
        {
            // create a new map entry
            auto mapEntryVec = SignalHistoryBuffer<T>{};
            try
            {
                signalHistoryBufferVar mapEntry = mapEntryVec;
                mSignalBuffers.insert( { signalID, mapEntry } );
            }
            catch ( ... )
            {
                FWE_LOG_ERROR( "Cannot insert the signalHistoryBuffer for Signal " + std::to_string( signalID ) );
                return nullptr;
            }
        }

        try
        {
            auto signalBufferPtr = mSignalBuffers.find( signalID );
            if ( signalBufferPtr != mSignalBuffers.end() )
            {
                resVec = boost::get<SignalHistoryBuffer<T>>( &( signalBufferPtr->second ) );
            }
        }
        catch ( ... )
        {
            FWE_LOG_ERROR( "Cannot retrieve the signalHistoryBuffer for Signal " + std::to_string( signalID ) );
        }
        return resVec;
    }

    // This struct contains information about one time condition for a specific period
    struct TimebasedCondition
    {
        TimePoint lastTriggerTime{ 0, 0 };
        std::set<SignalID> signalIDsToSend;
    };

    // Queue to send responses to the LastKnownState commands
    std::shared_ptr<DataSenderQueue> mCommandResponses;

    std::shared_ptr<CacheAndPersist> mSchemaPersistency;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    struct StateTemplate
    {
        std::shared_ptr<const StateTemplateInformation> info;
        bool activated{ false };
        bool sendSnapshot{ false };
        Timestamp deactivateAfterMonotonicTimeMs{ 0 };
        // Map of signal update period. This only contains signals that have update strategy as period
        std::unordered_map<SignalID, Timestamp> signalUpdatePeriodMap;
        // Map from period to time based condition
        TimebasedCondition timeBasedCondition;
        // This is a buffer to hold the signals to be sent out
        std::vector<CollectedSignal> changedSignals;
    };
    std::map<SyncID, StateTemplate> mStateTemplates;

    void deactivateStateTemplate( StateTemplate &stateTemplate );

    struct PersistedStateTemplateMetadata
    {
        SyncID stateTemplateId;
        bool activated{ false };
        // We need to store the system time since the monotonic clock can be reset between system or
        // application restarts.
        Timestamp deactivateAfterSystemTimeMs{ 0 };
    };
    Json::Value mPersistedMetadata;

    /**
     * @brief Reads the persisted metadata (if any) from the persistency storage
     *
     * The metadata isn't applied to existing state templates. This only parses the content and
     * stores the result for later use.
     */
    void restorePersistedMetadata();

    /**
     * @brief Update the metadata for a specific state template and immediately store it
     *
     * @param metadata The modified metadata to update
     */
    void updatePersistedMetadata( const PersistedStateTemplateMetadata &metadata );

    /**
     * @brief Remove the persisted metadata for the given state template IDs and store the final result
     *
     * @param stateTemplateIds The IDs of the state templates to remove
     */
    void removePersistedMetadata( const std::vector<SyncID> &stateTemplateIds );

    /**
     * @brief Helper function to get some fields from the json metadata
     *
     * @param stateTemplateId The ID of the state template
     * @param currentTime The current time, used to calculate the monotonic time for auto deactivation
     * @param activated Output variable which will contain the updated activation state (if any)
     * @param deactivateAfterMonotonicTimeMs Output variable which will contain the updated time to auto deactivate the
     * state template
     */
    void extractMetadataFields( const SyncID &stateTemplateId,
                                const TimePoint &currentTime,
                                bool &activated,
                                Timestamp &deactivateAfterMonotonicTimeMs );
};

template <typename T>
void
// coverity[misra_cpp_2008_rule_14_7_1_violation] function will be instantiated in incoming module
LastKnownStateInspector::inspectNewSignal( SignalID id, const TimePoint &receiveTime, T value )
{
    if ( mSignalBuffers.find( id ) == mSignalBuffers.end() || mSignalBuffers[id].empty() )
    {
        // Signal not collected by any active condition
        return;
    }

    // Iterate through all sampling intervals of the signal
    SignalHistoryBuffer<T> *signalHistoryBufferPtr = nullptr;
    signalHistoryBufferPtr = getSignalHistoryBufferPtr<T>( id );
    if ( signalHistoryBufferPtr == nullptr )
    {
        // Invalid access to the map Buffer datatype
        FWE_LOG_WARN( "Unable to locate the signal history buffer for signal " + std::to_string( id ) );
        return;
    }

    for ( auto &stateTemplate : mStateTemplates )
    {
        auto updateStrategy = stateTemplate.second.info->updateStrategy;
        for ( auto &signal : stateTemplate.second.info->signals )
        {
            if ( id != signal.signalID )
            {
                continue;
            }

            if ( ( updateStrategy == LastKnownStateUpdateStrategy::ON_CHANGE ) && stateTemplate.second.activated )
            {
                // Under one of the following conditions, we will send out update for this signal
                // 1. signal history buffer is empty previously, or
                // 2. signal history buffer is not empty and signal data type is double / float and the
                //    difference between prev and latest value is greater than EVAL_EQUAL_DISTANCE
                // 3. signal history buffer is not empty and signal data type is non-double / non-float and
                //    the prev value is not equal to latest value.
                if ( ( signalHistoryBufferPtr->mCounter == 0 ) ||
                     ( ( ( mSignalToBufferTypeMap[id] == SignalType::DOUBLE ) ||
                         ( mSignalToBufferTypeMap[id] == SignalType::FLOAT ) )
                           ? ( std::abs( static_cast<double>( value ) -
                                         static_cast<double>(
                                             signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition]
                                                 .mValue ) ) > EVAL_EQUAL_DISTANCE() )
                           : ( value !=
                               signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].mValue ) ) )
                {
                    // We shall immediately publish this to the buffer
                    FWE_LOG_TRACE( "Collecting signal with ID " + std::to_string( id ) + " for on change policy" );
                    stateTemplate.second.changedSignals.emplace_back(
                        CollectedSignal( id, receiveTime.systemTimeMs, value, mSignalToBufferTypeMap[id] ) );
                    TraceModule::get().incrementVariable( TraceVariable::LAST_KNOWN_STATE_ON_CHANGE_UPDATES );
                }
            }
            else if ( updateStrategy == LastKnownStateUpdateStrategy::PERIODIC )
            {
                stateTemplate.second.timeBasedCondition.signalIDsToSend.emplace( id );
            }
        }
    }

    signalHistoryBufferPtr->mCurrentPosition++;
    if ( signalHistoryBufferPtr->mCurrentPosition >= signalHistoryBufferPtr->mSize )
    {
        signalHistoryBufferPtr->mCurrentPosition = 0;
    }
    // Add this new sample to the signal history buffer
    signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].mValue = value;
    signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].mTimestamp = receiveTime.systemTimeMs;
    signalHistoryBufferPtr->mBuffer[signalHistoryBufferPtr->mCurrentPosition].setAlreadyConsumed( ALL_CONDITIONS,
                                                                                                  false );
    if ( signalHistoryBufferPtr->mCounter < MAX_SIGNAL_HISTORY_BUFFER_SIZE )
    {
        signalHistoryBufferPtr->mCounter++;
    }
    signalHistoryBufferPtr->mLastSample = receiveTime;
}

} // namespace IoTFleetWise
} // namespace Aws
