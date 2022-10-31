// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "CANInterfaceIDTranslator.h"
#include "ClockHandler.h"
#include "CollectionSchemeManagementListener.h"
#include "IActiveConditionProcessor.h"
#include "IActiveDecoderDictionaryListener.h"
#include "ICollectionSchemeList.h"
#include "ICollectionSchemeManager.h"
#include "IDecoderManifest.h"
#include "Listener.h"
#include "LoggingModule.h"
#include "SchemaListener.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include <atomic>
#include <map>
#include <mutex>
#include <queue>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Linux;

using SchemaListenerPtr = std::shared_ptr<SchemaListener>;

/* TimeData is used in mTimeline, the second parameter in the pair is a CollectionScheme ID */
using TimeData = std::pair<TimePointInMsec, std::string>;

/**
 * @brief main CollectionScheme Management entity - responsible for the following:
 * 1. Listens to collectionScheme ingestion to get CollectionSchemeList and DecoderManifest
 * 2. Process CollectionSchemeList to generate timeLine in chronological order, organize CollectionSchemeList into
   Enabled and Idle lists;
 * 3. Wait for timer to elapse on TimePointInMsecond along timeLine chronologically, re-org Enabled and Idle list;
 * 4. Extract decoding dictionary and propagate to Vehicle Data Consumer;
 * 5. Extract Inspection Matrix and propagate to Inspection Engine;
 * 6. Delete expired collectionSchemes from Enabled list, or removed collectionScheme from existing list per Cloud
  request.
 */

class CollectionSchemeManager : public ICollectionSchemeManager,
                                public CollectionSchemeManagementListener,
                                public ThreadListeners<IActiveDecoderDictionaryListener>,
                                public ThreadListeners<IActiveConditionProcessor>
{
public:
    using ThreadListeners<IActiveDecoderDictionaryListener>::subscribeListener;
    using ThreadListeners<IActiveConditionProcessor>::subscribeListener;

    CollectionSchemeManager() = default;

    CollectionSchemeManager( std::string dm_id );

    CollectionSchemeManager( std::string dm_id,
                             std::map<std::string, ICollectionSchemePtr> mapEnabled,
                             std::map<std::string, ICollectionSchemePtr> mapIdle );

    ~CollectionSchemeManager() override;

    CollectionSchemeManager( const CollectionSchemeManager & ) = delete;
    CollectionSchemeManager &operator=( const CollectionSchemeManager & ) = delete;
    CollectionSchemeManager( CollectionSchemeManager && ) = delete;
    CollectionSchemeManager &operator=( CollectionSchemeManager && ) = delete;

    /**
     * @brief Initializes collectionScheme management session.
     *
     * @param checkinIntervalMsec checkin message interval in millisecond
     * @param schemaPersistencyPtr shared pointer to collectionSchemePersistency object
     * @param canIDTranslator used to translate the cloud used Interface ID to the the internal channel id
     * @return True if successful. False otherwise.
     */
    bool init( uint32_t checkinIntervalMsec,
               const std::shared_ptr<ICacheAndPersist> &schemaPersistencyPtr,
               CANInterfaceIDTranslator &canIDTranslator );
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
    void onCollectionSchemeUpdate( const ICollectionSchemeListPtr &collectionSchemeList ) override;

    /**
     * @brief callback for CollectionScheme Ingestion to send update of IDecoderManifest
     * @param decoderManifest a constant shared pointer to IDecoderManifest from CollectionScheme Ingestion
     *
     * This function simply moves pointers passed in from PI into CollectionSchemeManagement's object.
     *
     * This function runs in AWS IoT context, not in PM context. This function needs to return quickly.
     * A lock in the function is applied to handle the race condition between AwdIoT context and PM context.

     */
    void onDecoderManifestUpdate( const IDecoderManifestPtr &decoderManifest ) override;

    /**
     * @brief Used by the bootstrap to set the SchemaListener pointer to allow CollectionScheme Management to send data
     * to CollectionScheme Ingestion
     *
     * @param collectionSchemeIngestionListenerPtr
     */
    inline void
    setSchemaListenerPtr( const SchemaListenerPtr collectionSchemeIngestionListenerPtr )
    {
        mSchemaListenerPtr = collectionSchemeIngestionListenerPtr;
    }

    /**
     * @brief Used by the bootstrap to set the UseLocalDictionary Flag to ingore DM
     * updates through MQTT connection
     *
     * @param useLocalDictionaryIn
     */
    inline void
    setLocalDictionaryFlag( bool useLocalDictionaryIn )
    {
        mUseLocalDictionary = useLocalDictionaryIn;
    }

    /**
     * @brief Used by the bootstrap to set the DecoderManifestID when using local Dictionary
     *
     * @param currentDecoderManifestIDIn
     */
    inline void
    setDecoderManifestID( std::string currentDecoderManifestIDIn )
    {
        currentDecoderManifestID = std::move( currentDecoderManifestIDIn );
    }

private:
    using ThreadListeners<IActiveDecoderDictionaryListener>::notifyListeners;
    using ThreadListeners<IActiveConditionProcessor>::notifyListeners;

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

    /**
     * @brief template function for generate a message on an event for mLogger usage
     * Include Event printed in string msg, collectionScheme ID, startTime, stopTime of the collectionScheme, and
     * current timestamp all in seconds.
     * @param msg string for log;
     * @param id collectionScheme id;
     * @param startTime startTime of the CollectionScheme;
     * @param stopTime stopTime of the CollectionScheme;
     * @param currTime time when main thread wakes up
     */
    static void printEventLogMsg( std::string &msg,
                                  const std::string &id,
                                  const TimePointInMsec &startTime,
                                  const TimePointInMsec &stopTime,
                                  const TimePointInMsec &currTime );

    /**
     * @brief supporting function for mLogger
     * Prints out enabled CollectionScheme ID string and Idle CollectionScheme ID string
     * @param enableStr string for enabled CollectionScheme IDs;
     * @param idleStr string for Idle CollectionScheme IDs;
     */
    void printExistingCollectionSchemes( std::string &enableStr, std::string &idleStr );

    /**
     * @brief supporting function for mLogger
     * Prints status when main thread wakes up from notification or timer return.
     * The status includes flag mUpdateAvailable, and currTime in seconds.
     * @param wakeupStr string to print status;
     */
    void printWakeupStatus( std::string &wakeupStr ) const;

    /**
     * @brief clean up collectionScheme maps and timeline
     *
     */
    void cleanupCollectionSchemes();

    /**
     * @brief Fill up the fields in ConditionWithCollectedData
     * To be called by inspectionMatrixExtractor
     *
     * @param collectionScheme the collectionScheme where the info is retrieved
     * @param conditionData object ConditionWithCollectedData to be filled
     */
    void addConditionData( const ICollectionSchemePtr &collectionScheme,
                           struct ConditionWithCollectedData &conditionData );

    /**
     * @brief initialize in mTimeLine to send checkin message
     */
    void prepareCheckinTimer();

    /**
     * @brief checks if there is any enabled or idle collectionScheme in the system
     * returns true when there is
     */
    bool isCollectionSchemeLoaded();

protected:
    bool rebuildMapsandTimeLine( const TimePointInMsec &currTime ) override;

    bool updateMapsandTimeLine( const TimePointInMsec &currTime ) override;

    bool checkTimeLine( const TimePointInMsec &currTime ) override;

    /**
     * @brief This function extract the decoder dictionary from decoder manifest and polices
     *
     * @param decoderDictionaryMap pass reference of the map of decoder dictionary. This map contains dictionaries for
     * different network types
     */
    void decoderDictionaryExtractor(
        std::map<VehicleDataSourceProtocol, std::shared_ptr<CANDecoderDictionary>> &decoderDictionaryMap );

    /**
     * @brief This function invoke all the listener for decoder dictionary update. The listener can be any types of
     * Networks
     *
     * @param decoderDictionaryMap pass reference of the map of decoder dictionary. This map contains dictionaries for
     * different network types
     */
    void decoderDictionaryUpdater(
        std::map<VehicleDataSourceProtocol, std::shared_ptr<CANDecoderDictionary>> &decoderDictionaryMap );

    void inspectionMatrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix ) override;

    void inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix ) override;

    bool retrieve( DataType retrieveType ) override;

    void store( DataType storeType ) override;

    bool processDecoderManifest() override;

    bool processCollectionScheme() override;

    bool sendCheckin() override;

    void updateAvailable() override;

private:
    // default checkin interval set to 5 mins
    static constexpr int DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND = 300000;
    // Checkin retry interval. Used issue checkins to the cloud as soon as possible, set to 5 seconds
    static constexpr int RETRY_CHECKIN_INTERVAL_IN_MILLISECOND = 5000;
    // checkIn ID in parallel to collectionScheme IDs
    static const std::string CHECKIN;
    // Supported Network Protocol. This list will expand when new protocol added
    static constexpr std::array<VehicleDataSourceProtocol, 2> SUPPORTED_NETWORK_PROTOCOL = {
        VehicleDataSourceProtocol::RAW_SOCKET, VehicleDataSourceProtocol::OBD };

    Thread mThread;
    // Atomic flag to signal the state of main thread. If true, we should stop
    std::atomic<bool> mShouldStop{ false };
    // mutex that protects the thread
    mutable std::mutex mThreadMutex;

    // Platform signal that wakes up main thread
    Platform::Linux::Signal mWait;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    LoggingModule mLogger;

    // Shared pointer to a SchemaListener Object allow CollectionSchemeManagement to send data to Schema
    SchemaListenerPtr mSchemaListenerPtr;

    // Idle collectionScheme collection
    std::map<std::string, ICollectionSchemePtr> mIdleCollectionSchemeMap;

    // Enabled collectionScheme collection
    std::map<std::string, ICollectionSchemePtr> mEnabledCollectionSchemeMap;

    // ID for the decoder manifest currently in use
    std::string currentDecoderManifestID;

    // Time interval in ms to send checkin message
    uint64_t mCheckinIntervalInMsec{ DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND };

protected:
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
    // CacheAndPersist object passed from K-Engine
    std::shared_ptr<ICacheAndPersist> mSchemaPersistency;
    // flag used to check if local dictionary is available
    bool mUseLocalDictionary{ false };

    CANInterfaceIDTranslator mCANIDTranslator;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
