// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANInterfaceIDTranslator.h"
#include "CacheAndPersist.h"
#include "CheckinSender.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "Listener.h"
#include "RawDataManager.h"
#include "Signal.h"
#include "SignalTypes.h"
#include "Thread.h"
#include "TimeTypes.h"
#include "VehicleDataSourceTypes.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "MessageTypes.h"
#include <unordered_map>
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
        std::function<void( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )>;

    /**
     * @brief Callback to notify the change of active collection schemes
     *
     * */
    using OnCollectionSchemeListChangeCallback =
        std::function<void( const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes )>;

    CollectionSchemeManager(
        std::shared_ptr<CacheAndPersist>
            schemaPersistencyPtr,                  /**< shared pointer to collectionSchemePersistency object */
        CANInterfaceIDTranslator &canIDTranslator, /**< canIDTranslator used to translate the cloud used Interface
                                                     ID to the the internal channel id */
        std::shared_ptr<CheckinSender>
            checkinSender, /**< the checkin sender that needs to be updated with the current documents */
        std::shared_ptr<RawData::BufferManager> rawDataBufferManager =
            nullptr /**< rawDataBufferManager Optional manager to handle raw data. If not given, raw data
                       collection will be disabled */
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
    void onCollectionSchemeUpdate( const ICollectionSchemeListPtr &collectionSchemeList );

    /**
     * @brief callback for CollectionScheme Ingestion to send update of IDecoderManifest
     * @param decoderManifest a constant shared pointer to IDecoderManifest from CollectionScheme Ingestion
     *
     * This function simply moves pointers passed in from PI into CollectionSchemeManagement's object.
     *
     * This function runs in AWS IoT context, not in PM context. This function needs to return quickly.
     * A lock in the function is applied to handle the race condition between AwdIoT context and PM context.

     */
    void onDecoderManifestUpdate( const IDecoderManifestPtr &decoderManifest );

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
        mActiveDecoderDictionaryChangeListeners.subscribe( callback );
    }

    /**
     * @brief Subscribe to changes in the inspection matrix
     * @param callback A function that will be called when the inspection matrix changes
     */
    void
    subscribeToInspectionMatrixChange( OnInspectionMatrixChangeCallback callback )
    {
        mInspectionMatrixChangeListeners.subscribe( callback );
    }

    /**
     * @brief Subscribe to changes in the collection scheme list
     * @param callback A function that will be called when the collection scheme list changes
     */
    void
    subscribeToCollectionSchemeListChange( OnCollectionSchemeListChangeCallback callback )
    {
        mCollectionSchemeListChangeListeners.subscribe( callback );
    }

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
     * @param data collectionScheme manager object
     */
    static void doWork( void *data );

    static TimePoint calculateMonotonicTime( const TimePoint &currTime, Timestamp systemTimeMs );

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
    void addConditionData( const ICollectionSchemePtr &collectionScheme, ConditionWithCollectedData &conditionData );

    /**
     * @brief checks if there is any enabled or idle collectionScheme in the system
     * returns true when there is
     */
    bool isCollectionSchemeLoaded();

    bool isCollectionSchemesInSyncWithDm();

    void extractCondition( const std::shared_ptr<InspectionMatrix> &inspectionMatrix,
                           const ICollectionSchemePtr &collectionScheme,
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
        std::shared_ptr<InspectionMatrix> inspectionMatrix /**< the inspection matrix that will be updated with the
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
        std::shared_ptr<Aws::IoTFleetWise::ComplexDataDecoderDictionary> complexDataDecoderDictionary,
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
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap );

    void matrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix );

    void inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix );

    bool retrieve( DataType retrieveType );

    void store( DataType storeType );

    bool processDecoderManifest();

    bool processCollectionScheme();

    void updateCheckinDocuments();

    void updateAvailable();

private:
    Thread mThread;
    // Atomic flag to signal the state of main thread. If true, we should stop
    std::atomic<bool> mShouldStop{ false };
    // mutex that protects the thread
    mutable std::mutex mThreadMutex;

    // Platform signal that wakes up main thread
    Signal mWait;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    std::shared_ptr<CheckinSender> mCheckinSender;

    // Shared pointer to a Raw Data Buffer Manager Object allow CollectionSchemeManagement to send BufferConfig to
    // the Manager
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;

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
    std::map<SyncID, ICollectionSchemePtr> mIdleCollectionSchemeMap;

    // Enabled collectionScheme collection
    std::map<SyncID, ICollectionSchemePtr> mEnabledCollectionSchemeMap;

    // ID for the decoder manifest currently in use
    SyncID mCurrentDecoderManifestID;

    /*
     * PM Local storage of CollectionSchemeList and mDecoderManifest so that PM can work on these objects
     * out of critical section
     */
    ICollectionSchemeListPtr mCollectionSchemeList;
    IDecoderManifestPtr mDecoderManifest;
    /* Timeline keeps track on StartTime and StopTime of all existing collectionSchemes */
    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> mTimeLine;
    /* lock used in callback functions onCollectionSchemeAvailable and onDecoderManifest */
    std::mutex mSchemaUpdateMutex;

    ThreadSafeListeners<OnActiveDecoderDictionaryChangeCallback> mActiveDecoderDictionaryChangeListeners;
    ThreadSafeListeners<OnInspectionMatrixChangeCallback> mInspectionMatrixChangeListeners;
    ThreadSafeListeners<OnCollectionSchemeListChangeCallback> mCollectionSchemeListChangeListeners;

    /*
     * parameters used in  onCollectionSchemeAvailable()
     * mCollectionSchemeAvailable: flag notifying collectionScheme update is available
     */
    bool mCollectionSchemeAvailable{ false };
    /*
     * parameters used in  onCollectionSchemeAvailable()
     * mCollectionSchemeListInput: a shared pointer of ICollectionSchemeList PI copies into
     */
    ICollectionSchemeListPtr mCollectionSchemeListInput;

    /*
     * parameters used in  onDecoderManifestAvailable()
     * mDecoderManifestAvailable: flag notifying decodermanifest update is available
     */
    bool mDecoderManifestAvailable{ false };
    /*
     * parameters used in  onDecoderManifestAvailable()
     * mDecoderManifestInput: a shared pointer of IDecoderManifest PI copies into
     */
    IDecoderManifestPtr mDecoderManifestInput;

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
