// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/Listener.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "aws/iotfleetwise/MessageTypes.h"
#endif

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "aws/iotfleetwise/LastKnownStateIngestion.h"
#include "aws/iotfleetwise/LastKnownStateTypes.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

/* TimeData is used in mTimeline, the second parameter in the pair is a CollectionScheme ID */
struct TimeData
{
    TimePoint time;
    SyncID id;

    bool
    operator>( const TimeData &other ) const
    {
        return ( this->time.monotonicTimeMs > other.time.monotonicTimeMs ) ||
               ( ( this->time.monotonicTimeMs == other.time.monotonicTimeMs ) && ( this->id > other.id ) );
    }
};

/**
 * @brief main CollectionScheme Management entity - responsible for the following:
 * 1. Listens to collectionScheme ingestion to get CollectionSchemeList and DecoderManifest
 * 2. Process CollectionSchemeList to generate timeLine in chronological order, organize CollectionSchemeList into
   Enabled and Idle lists;
 * 3. Wait for timer to elapse along timeLine chronologically, re-org Enabled and Idle list;
 * 4. Extract decoding dictionary and propagate to Vehicle Data Consumer;
 * 5. Extract Inspection Matrix and propagate to Inspection Engine;
 * 6. Delete expired collectionSchemes from Enabled list, or removed collectionScheme from existing list per Cloud
  request.
 * 7. Notify other components about currently Enabled CollectionSchemes.
 */
class CollectionSchemeManager // NOLINT(clang-analyzer-optin.performance.Padding)
{
public:
    /**
     * @brief The callback function used to notify any listeners on change of Decoder Dictionary
     *
     * @param dictionary const shared pointer pointing to a constant decoder dictionary
     * @param networkProtocol network protocol type indicating which type of decoder dictionary it's updating
     */
    using OnActiveDecoderDictionaryChangeCallback =
        std::function<void( ConstDecoderDictionaryConstPtr &dictionary, VehicleDataSourceProtocol networkProtocol )>;

    /**
     * @brief Callback to notify the change of active conditions for example by rebuilding buffers
     *
     * This function should be called as rarely as possible.
     * All condition should fulfill the restriction like max signal id or equation depth.
     * After this call all cached signal values that were not published are deleted
     * @param inspectionMatrix all currently active Conditions
     * @return true if valid conditions were handed over
     * */
    using OnInspectionMatrixChangeCallback =
        std::function<void( std::shared_ptr<const InspectionMatrix> inspectionMatrix )>;

    /**
     * @brief   Callback to notify the change of fetch configuration matrix.
     * Need to be used along with inspection matrix change callback.
     *
     * @param   fetchMatrix - all currently active fetch configuration.
     * @return  none
     * */
    using OnFetchMatrixChangeCallback = std::function<void( std::shared_ptr<const FetchMatrix> fetchMatrix )>;

    /**
     * @brief Callback to notify the change of active collection schemes
     *
     * */
    using OnCollectionSchemeListChangeCallback =
        std::function<void( std::shared_ptr<const ActiveCollectionSchemes> activeCollectionSchemes )>;

    /**
     * @brief Callback to notify about the change of custom signal decoder format map.
     * It is used to notify data consumers, not the data sources.
     * @param currentDecoderManifestID sync id of the decoder manifest that is used
     * @param customSignalDecoderFormatMap const shared pointer pointing to a constant custom signal decoder format map
     * */
    using OnCustomSignalDecoderFormatMapChangeCallback = std::function<void(
        const SyncID &currentDecoderManifestID,
        std::shared_ptr<const SignalIDToCustomSignalDecoderFormatMap> customSignalDecoderFormatMap )>;

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    using OnStateTemplatesChangeCallback = std::function<void( std::shared_ptr<StateTemplateList> stateTemplates )>;
#endif

#ifdef FWE_FEATURE_REMOTE_COMMANDS
    using GetActuatorNamesCallback = std::function<std::unordered_map<InterfaceID, std::vector<std::string>>()>;
#endif

    CollectionSchemeManager(
        std::shared_ptr<CacheAndPersist>
            schemaPersistencyPtr,                  /**< shared pointer to collectionSchemePersistency object */
        CANInterfaceIDTranslator &canIDTranslator, /**< canIDTranslator used to translate the cloud used Interface
                                                     ID to the the internal channel id */
        std::shared_ptr<CheckinSender>
            checkinSender, /**< the checkin sender that needs to be updated with the current documents */
        RawData::BufferManager *rawDataBufferManager =
            nullptr /**< rawDataBufferManager Optional manager to handle raw data. If not given, raw data
                       collection will be disabled */
#ifdef FWE_FEATURE_REMOTE_COMMANDS
        ,
        GetActuatorNamesCallback getActuatorNamesCallback =
            nullptr /**< Callback to get the names of actuators. TODO: Once the decoder manifest supports the
                       READ/WRITE/READ_WRITE indication for each signal, this can be removed */
#endif
        ,
        uint32_t idleTimeMs = 0 /**< Max time the thread will wait for until the next iteration to check whether there
                               is any collection scheme to be started or expired. This is most important in situations
                               where the system time jumps. In such case you should expect collection schemes to
                               start/expire with a delay of up to idleTimeMs */
    );

    ~CollectionSchemeManager();

    CollectionSchemeManager( const CollectionSchemeManager & ) = delete;
    CollectionSchemeManager &operator=( const CollectionSchemeManager & ) = delete;
    CollectionSchemeManager( CollectionSchemeManager && ) = delete;
    CollectionSchemeManager &operator=( CollectionSchemeManager && ) = delete;

    /**
     * @brief Sets up connection with CollectionScheme Ingestion and start main thread.
     * @return True if successful. False otherwise.
     */
    bool connect();

    /**
     * @brief Disconnect with CollectionScheme Ingestion and stops main thread.
     * @return True if successful. False otherwise.
     */
    bool disconnect();

    /**
     * @brief Checks that the worker thread is healthy and consuming data.
     */
    bool isAlive();

    /**
     * @brief callback for CollectionScheme Ingestion to send update of ICollectionSchemeList
     * @param collectionSchemeList a constant shared pointer to ICollectionSchemeList from CollectionScheme Ingestion
     *
     * This function simply moves pointers passed in from PI into CollectionSchemeManagement's object.
     *
     * This function runs in AWS IoT context, not in PM context. This function needs to return quickly.
     * A lock in the function is applied to handle the race condition between AwdIoT context and PM context.
     *
     */
    void onCollectionSchemeUpdate( std::shared_ptr<ICollectionSchemeList> collectionSchemeList );

    /**
     * @brief callback for CollectionScheme Ingestion to send update of IDecoderManifest
     * @param decoderManifest a constant shared pointer to IDecoderManifest from CollectionScheme Ingestion
     *
     * This function simply moves pointers passed in from PI into CollectionSchemeManagement's object.
     *
     * This function runs in AWS IoT context, not in PM context. This function needs to return quickly.
     * A lock in the function is applied to handle the race condition between AwdIoT context and PM context.

     */
    void onDecoderManifestUpdate( std::shared_ptr<IDecoderManifest> decoderManifest );

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    void onStateTemplatesChanged( std::shared_ptr<LastKnownStateIngestion> lastKnownStateIngestion );
#endif

    /**
     * @brief Returns the current list of collection scheme ARNs
     * @return List of collection scheme ARNs
     */
    std::vector<SyncID> getCollectionSchemeArns();

    /**
     * @brief Subscribe to changes in the active decoder dictionary
     * @param callback A function that will be called when the active dictionary changes
     */
    void
    subscribeToActiveDecoderDictionaryChange( OnActiveDecoderDictionaryChangeCallback callback )
    {
        mActiveDecoderDictionaryChangeListeners.subscribe( std::move( callback ) );
    }

    /**
     * @brief Subscribe to changes in the inspection matrix
     * @param callback A function that will be called when the inspection matrix changes
     */
    void
    subscribeToInspectionMatrixChange( OnInspectionMatrixChangeCallback callback )
    {
        mInspectionMatrixChangeListeners.subscribe( std::move( callback ) );
    }

    /**
     * @brief   Subscribe to changes in the fetch matrix
     * @param   callback - function that will be called when the fetch matrix changes
     */
    void
    subscribeToFetchMatrixChange( OnFetchMatrixChangeCallback callback )
    {
        mFetchMatrixChangeListeners.subscribe( std::move( callback ) );
    }

    /**
     * @brief Subscribe to changes in the collection scheme list
     * @param callback A function that will be called when the collection scheme list changes
     */
    void
    subscribeToCollectionSchemeListChange( OnCollectionSchemeListChangeCallback callback )
    {
        mCollectionSchemeListChangeListeners.subscribe( std::move( callback ) );
    }

    /**
     * @brief Subscribe to changes in the custom signal decoder format map
     * @param callback A function that will be called when the custom signal decoder format map changes
     */
    void
    subscribeToCustomSignalDecoderFormatMapChange( OnCustomSignalDecoderFormatMapChangeCallback callback )
    {
        mCustomSignalDecoderFormatMapChangeListeners.subscribe( std::move( callback ) );
    }

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    void
    subscribeToStateTemplatesChange( OnStateTemplatesChangeCallback callback )
    {
        mStateTemplatesChangeListeners.subscribe( std::move( callback ) );
    }
#endif

private:
    /**
     * @brief Starts main thread
     * @return True if successful. False otherwise.
     */
    bool start();

    /**
     * @brief Stops main thread
     * @return True if successful. False otherwise.
     */
    bool stop();

    /**
     * @brief Checks if stop request is made
     * @return True if request is made. False otherwise.
     */
    bool shouldStop() const;

    /**
     * @brief Function that runs on main thread
     */
    void doWork();

    /**
     * @brief template function for generate a message on an event for logging
     * Include Event printed in string msg, collectionScheme ID, startTime, stopTime of the collectionScheme, and
     * current timestamp all in seconds.
     * @param msg string for log;
     * @param id collectionScheme id;
     * @param startTime startTime of the CollectionScheme;
     * @param stopTime stopTime of the CollectionScheme;
     * @param currTime time when main thread wakes up
     */
    static void printEventLogMsg( std::string &msg,
                                  const SyncID &id,
                                  const Timestamp &startTime,
                                  const Timestamp &stopTime,
                                  const TimePoint &currTime );

    /**
     * @brief supporting function for logging
     * Prints out enabled CollectionScheme ID string and Idle CollectionScheme ID string
     * @param enableStr string for enabled CollectionScheme IDs;
     * @param idleStr string for Idle CollectionScheme IDs;
     */
    void printExistingCollectionSchemes( std::string &enableStr, std::string &idleStr );

    /**
     * @brief supporting function for logging
     * Prints status when main thread wakes up from notification or timer return.
     * The status includes flag mUpdateAvailable, and currTime in seconds.
     * @param wakeupStr string to print status;
     */
    void printWakeupStatus( std::string &wakeupStr ) const;

    /**
     * @brief Fill up the fields in ConditionWithCollectedData
     * To be called by matrixExtractor
     *
     * @param collectionScheme the collectionScheme where the info is retrieved
     * @param conditionData object ConditionWithCollectedData to be filled
     */
    void addConditionData( const ICollectionScheme &collectionScheme, ConditionWithCollectedData &conditionData );

    /**
     * @brief checks if there is any enabled or idle collectionScheme in the system
     * returns true when there is
     */
    bool isCollectionSchemeLoaded();

    bool isCollectionSchemesInSyncWithDm();

    void extractCondition( InspectionMatrix &inspectionMatrix,
                           const ICollectionScheme &collectionScheme,
                           std::vector<const ExpressionNode *> &nodes,
                           std::map<const ExpressionNode *, uint32_t> &nodeToIndexMap,
                           uint32_t &index,
                           const ExpressionNode *initialNode );

protected:
    bool rebuildMapsandTimeLine( const TimePoint &currTime );

    bool updateMapsandTimeLine( const TimePoint &currTime );

    bool checkTimeLine( const TimePoint &currTime );

    /**
     * @brief This function extract the decoder dictionary from decoder manifest and polices
     */
    void decoderDictionaryExtractor(
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>>
            &decoderDictionaryMap /**< pass reference of the map of decoder dictionary. This map contains dictionaries
                                     for different network types */
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        ,
        InspectionMatrix &inspectionMatrix /**< the inspection matrix that will be updated with the
                                                              right signal types for partial signals */
#endif
    );

    void addSignalToDecoderDictionaryMap(
        SignalID signalId,
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        ,
        std::unordered_map<SignalID, SignalType> &partialSignalTypes,
        SignalID topLevelSignalId = INVALID_SIGNAL_ID,
        SignalPath signalPath = SignalPath()
#endif
    );

    /**
     * @brief Fills up and creates the BufferConfig with string signals
     * @param updatedSignals map of the signals that will be updated by Raw Buffer Manager
     */
    void updateRawDataBufferConfigStringSignals(
        std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief only executed from within decoderDictionaryExtractor to put a complex signal into the dictionary
     * @param complexSignal the signal to put in dictionary. If complexSignal.mSignalId is not set it will be set to
     * signalID and the object will be initialized
     * @param signalID the signal ID for which complexSignal should be used
     * @param partialSignalID the ID that should be used for a partial signal. Only used if signalPath is not empty
     * @param signalPath if not empty this signal is a partialSignal and partialSignalID will be use
     * @param complexSignalRootType the root complex type of this signal
     * @param partialSignalTypes the map that will be updated with the signal types for partial signals
     */
    void putComplexSignalInDictionary( ComplexDataMessageFormat &complexSignal,
                                       SignalID signalID,
                                       PartialSignalID partialSignalID,
                                       SignalPath &signalPath,
                                       ComplexDataTypeId complexSignalRootType,
                                       std::unordered_map<SignalID, SignalType> &partialSignalTypes );

    /**
     * @brief Fills up and creates the BufferConfig with complex signals
     * @param complexDataDecoderDictionary current complex data decoder dict
     * @param updatedSignals map of the signals that will be updated by Raw Buffer Manager
     */
    void updateRawDataBufferConfigComplexSignals(
        Aws::IoTFleetWise::ComplexDataDecoderDictionary *complexDataDecoderDictionary,
        std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals );
#endif

    /**
     * @brief This function invoke all the listener for decoder dictionary update. The listener can be any types of
     * Networks
     *
     * @param decoderDictionaryMap pass reference of the map of decoder dictionary. This map contains dictionaries for
     * different network types
     */
    void decoderDictionaryUpdater(
        const std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap );

    void matrixExtractor( InspectionMatrix &inspectionMatrix, FetchMatrix &fetchMatrix );

    void inspectionMatrixUpdater( std::shared_ptr<const InspectionMatrix> inspectionMatrix );

    void fetchMatrixUpdater( std::shared_ptr<const FetchMatrix> fetchMatrix );

    bool retrieve( DataType retrieveType );

    void store( DataType storeType );

    bool processDecoderManifest();

    bool processCollectionScheme();

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    bool processStateTemplates();

    std::shared_ptr<StateTemplateList> lastKnownStateExtractor();
    void lastKnownStateUpdater( std::shared_ptr<StateTemplateList> stateTemplates );
#endif

    void updateCheckinDocuments();

    void updateAvailable();

private:
    static constexpr uint32_t DEFAULT_THREAD_IDLE_TIME_MS = 1000;

    Thread mThread;
    // Atomic flag to signal the state of main thread. If true, we should stop
    std::atomic<bool> mShouldStop{ false };
    // mutex that protects the thread
    mutable std::mutex mThreadMutex;

    // Platform signal that wakes up main thread
    Signal mWait;
    uint32_t mIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    std::shared_ptr<CheckinSender> mCheckinSender;

    // Shared pointer to a Raw Data Buffer Manager Object allow CollectionSchemeManagement to send BufferConfig to
    // the Manager
    RawData::BufferManager *mRawDataBufferManager;

    // Builds vector of ActiveCollectionSchemes and notifies listeners about the update
    void updateActiveCollectionSchemeListeners();

    // Get the Signal Type from DM
    inline SignalType
    getSignalType( const SignalID signalID )
    {
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        // For internal signals we won't find the type in the decoder manifest. The type will be
        // determined later after both collection schemes and decoder manifest are processed.
        if ( ( signalID & INTERNAL_SIGNAL_ID_BITMASK ) != 0 )
        {
            return SignalType::UNKNOWN;
        }
#endif
        return mDecoderManifest->getSignalType( signalID );
    }

protected:
    // Idle collectionScheme collection
    std::map<SyncID, std::shared_ptr<ICollectionScheme>> mIdleCollectionSchemeMap;

    // Enabled collectionScheme collection
    std::map<SyncID, std::shared_ptr<ICollectionScheme>> mEnabledCollectionSchemeMap;

    // ID for the decoder manifest currently in use
    SyncID mCurrentDecoderManifestID;

    /*
     * PM Local storage of CollectionSchemeList and mDecoderManifest so that PM can work on these objects
     * out of critical section
     */
    std::shared_ptr<ICollectionSchemeList> mCollectionSchemeList;
    std::shared_ptr<IDecoderManifest> mDecoderManifest;
    /* Timeline keeps track on StartTime and StopTime of all existing collectionSchemes */
    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> mTimeLine;
    /* lock used in callback functions onCollectionSchemeAvailable and onDecoderManifest */
    std::mutex mSchemaUpdateMutex;

    ThreadSafeListeners<OnActiveDecoderDictionaryChangeCallback> mActiveDecoderDictionaryChangeListeners;
    ThreadSafeListeners<OnInspectionMatrixChangeCallback> mInspectionMatrixChangeListeners;
    ThreadSafeListeners<OnFetchMatrixChangeCallback> mFetchMatrixChangeListeners;
    ThreadSafeListeners<OnCollectionSchemeListChangeCallback> mCollectionSchemeListChangeListeners;
    ThreadSafeListeners<OnCustomSignalDecoderFormatMapChangeCallback> mCustomSignalDecoderFormatMapChangeListeners;
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    ThreadSafeListeners<OnStateTemplatesChangeCallback> mStateTemplatesChangeListeners;
#endif

    /*
     * parameters used in  onCollectionSchemeAvailable()
     * mCollectionSchemeAvailable: flag notifying collectionScheme update is available
     */
    bool mCollectionSchemeAvailable{ false };
    /*
     * parameters used in  onCollectionSchemeAvailable()
     * mCollectionSchemeListInput: a shared pointer of ICollectionSchemeList PI copies into
     */
    std::shared_ptr<ICollectionSchemeList> mCollectionSchemeListInput;

    /*
     * parameters used in  onDecoderManifestAvailable()
     * mDecoderManifestAvailable: flag notifying decodermanifest update is available
     */
    bool mDecoderManifestAvailable{ false };
    /*
     * parameters used in  onDecoderManifestAvailable()
     * mDecoderManifestInput: a shared pointer of IDecoderManifest PI copies into
     */
    std::shared_ptr<IDecoderManifest> mDecoderManifestInput;

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    bool mStateTemplatesAvailable{ false };
    bool mProcessStateTemplates{ false };
    std::shared_ptr<LastKnownStateIngestion> mLastKnownStateIngestionInput;
    std::shared_ptr<LastKnownStateIngestion> mLastKnownStateIngestion;
    std::unordered_map<SyncID, std::shared_ptr<const StateTemplateInformation>> mStateTemplates;
    uint64_t mLastStateTemplatesDiffVersion{ 0 };
#endif

#ifdef FWE_FEATURE_REMOTE_COMMANDS
    GetActuatorNamesCallback mGetActuatorNamesCallback;
#endif

    // flag used by main thread to check if collectionScheme needs to be processed
    bool mProcessCollectionScheme{ false };
    // flag used by main thread to check if DM needs to be processed
    bool mProcessDecoderManifest{ false };
    // CacheAndPersist object passed from IoTFleetWiseEngine
    std::shared_ptr<CacheAndPersist> mSchemaPersistency;

    CANInterfaceIDTranslator mCANIDTranslator;
};

} // namespace IoTFleetWise
} // namespace Aws
