// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CollectionSchemeManager.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <chrono>
#include <string>
#include <unordered_set>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

const std::string CollectionSchemeManager::CHECKIN = "Checkin";
CollectionSchemeManager::CollectionSchemeManager( std::string dm_id )
    : mCurrentDecoderManifestID( std::move( dm_id ) )
{
}

CollectionSchemeManager::CollectionSchemeManager( std::string dm_id,
                                                  std::map<std::string, ICollectionSchemePtr> mapEnabled,
                                                  std::map<std::string, ICollectionSchemePtr> mapIdle )
    : mIdleCollectionSchemeMap( std::move( mapIdle ) )
    , mEnabledCollectionSchemeMap( std::move( mapEnabled ) )
    , mCurrentDecoderManifestID( std::move( dm_id ) )
{
}

CollectionSchemeManager::~CollectionSchemeManager()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
    mEnabledCollectionSchemeMap.clear();
    mIdleCollectionSchemeMap.clear();
    while ( !mTimeLine.empty() )
    {
        mTimeLine.pop();
    }
}

bool
CollectionSchemeManager::start()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        FWE_LOG_ERROR( "Thread failed to start" );
    }
    else
    {
        FWE_LOG_INFO( "Thread started" );
        mThread.setThreadName( "fwDMColSchMngr" );
    }
    return mThread.isValid();
}

bool
CollectionSchemeManager::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    /*
     * When main thread is servicing a collectionScheme, it sets up timer
     * and wakes up only when timer expires. If main thread needs to
     * be stopped any time, use notify() to wake up
     * immediately.
     */
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_INFO( "Collection Scheme Thread stopped" );
    return true;
}

bool
CollectionSchemeManager::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

/* supporting functions for logging */
void
CollectionSchemeManager::printEventLogMsg( std::string &msg,
                                           const std::string &id,
                                           const Timestamp &startTime,
                                           const Timestamp &stopTime,
                                           const TimePoint &currTime )
{
    msg += "ID( " + id + " )";
    msg += "Start( " + std::to_string( startTime ) + " milliseconds )";
    msg += "Stop( " + std::to_string( stopTime ) + " milliseconds )";
    msg += "at Current System Time ( " + std::to_string( currTime.systemTimeMs ) + " milliseconds ).";
    msg += "at Current Monotonic Time ( " + std::to_string( currTime.monotonicTimeMs ) + " milliseconds ).";
}

void
CollectionSchemeManager::printExistingCollectionSchemes( std::string &enableStr, std::string &idleStr )
{
    enableStr = "Enabled: ";
    idleStr = "Idle: ";
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); it++ )
    {
        enableStr += it->second->getCollectionSchemeID();
        enableStr += ' ';
    }
    for ( auto it = mIdleCollectionSchemeMap.begin(); it != mIdleCollectionSchemeMap.end(); it++ )
    {
        idleStr += it->second->getCollectionSchemeID();
        idleStr += ' ';
    }
}

void
CollectionSchemeManager::printWakeupStatus( std::string &wakeupStr ) const
{
    wakeupStr = "Waking up to update the CollectionScheme: ";
    wakeupStr += mProcessCollectionScheme ? "Yes" : "No";
    wakeupStr += " and the DecoderManifest: ";
    wakeupStr += mProcessDecoderManifest ? "Yes" : "No";
}

// Clears both enabled collectionScheme map and idle collectionScheme map
// removes all dataPair from mTimeLine except for CHECKIN
void
CollectionSchemeManager::cleanupCollectionSchemes()
{
    if ( mEnabledCollectionSchemeMap.empty() && mIdleCollectionSchemeMap.empty() )
    {
        // already cleaned up
        return;
    }
    mEnabledCollectionSchemeMap.clear();
    mIdleCollectionSchemeMap.clear();

    // when cleaning up mTimeLine checkIn event needs to be preserved
    TimeData saveTimeData = { { 0, 0 }, "" };
    while ( !mTimeLine.empty() )
    {
        if ( mTimeLine.top().id == CHECKIN )
        {
            saveTimeData = mTimeLine.top();
        }
        mTimeLine.pop();
    }
    if ( saveTimeData.time.monotonicTimeMs != 0 )
    {
        mTimeLine.push( saveTimeData );
    }
}

void
CollectionSchemeManager::doWork( void *data )
{
    CollectionSchemeManager *collectionSchemeManager = static_cast<CollectionSchemeManager *>( data );
    bool enabledCollectionSchemeMapChanged = false;

    // Set up timer for checkin messages
    collectionSchemeManager->prepareCheckinTimer();
    // Retrieve collectionSchemeList and decoderManifest from persistent storage
    static_cast<void>( collectionSchemeManager->retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    static_cast<void>( collectionSchemeManager->retrieve( DataType::DECODER_MANIFEST ) );
    while ( true )
    {
        if ( collectionSchemeManager->mProcessDecoderManifest )
        {
            collectionSchemeManager->mProcessDecoderManifest = false;
            TraceModule::get().sectionBegin( TraceSection::MANAGER_DECODER_BUILD );
            if ( collectionSchemeManager->processDecoderManifest() )
            {
                enabledCollectionSchemeMapChanged = true;
            }
            TraceModule::get().sectionEnd( TraceSection::MANAGER_DECODER_BUILD );
        }
        if ( collectionSchemeManager->mProcessCollectionScheme )
        {
            collectionSchemeManager->mProcessCollectionScheme = false;
            TraceModule::get().sectionBegin( TraceSection::MANAGER_COLLECTION_BUILD );
            if ( collectionSchemeManager->processCollectionScheme() )
            {
                enabledCollectionSchemeMapChanged = true;
            }
            TraceModule::get().sectionEnd( TraceSection::MANAGER_COLLECTION_BUILD );
        }
        auto checkTime = collectionSchemeManager->mClock->timeSinceEpoch();
        if ( collectionSchemeManager->checkTimeLine( checkTime ) )
        {
            enabledCollectionSchemeMapChanged = true;
        }
        if ( enabledCollectionSchemeMapChanged )
        {
            TraceModule::get().sectionBegin( TraceSection::MANAGER_EXTRACTION );
            TraceModule::get().sectionBegin( TraceSection::COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA );
            FWE_LOG_TRACE(

                "Start extraction because of changed active collection schemes at system time " +
                std::to_string( checkTime.systemTimeMs ) );
            /*
             * Extract InspectionMatrix from mEnabledCollectionSchemeMap
             *
             * input: mEnabledCollectionSchemeMap
             * output: shared_ptr to InspectionMatrix
             *
             * Then, propagate inspection matrix to Inspection engine
             */
            enabledCollectionSchemeMapChanged = false;
            auto inspectionMatrixOutput = std::make_shared<InspectionMatrix>();
            collectionSchemeManager->inspectionMatrixExtractor( inspectionMatrixOutput );
            collectionSchemeManager->inspectionMatrixUpdater( inspectionMatrixOutput );
            /*
             * extract decoder dictionary
             * input: mDecoderManifest
             * output: shared_ptr to decoderDictionary
             *
             * the propagate the output to Vehicle Data Consumers
             */
            std::map<VehicleDataSourceProtocol, std::shared_ptr<CANDecoderDictionary>> decoderDictionaryMap;
            collectionSchemeManager->decoderDictionaryExtractor( decoderDictionaryMap );
            // Publish decoder dictionaries update to all listeners
            collectionSchemeManager->decoderDictionaryUpdater( decoderDictionaryMap );
            // coverity[check_return : SUPPRESS]
            std::string decoderCanChannels = std::to_string(
                ( decoderDictionaryMap.find( VehicleDataSourceProtocol::RAW_SOCKET ) != decoderDictionaryMap.end() &&
                  decoderDictionaryMap[VehicleDataSourceProtocol::RAW_SOCKET] != nullptr )
                    ? decoderDictionaryMap[VehicleDataSourceProtocol::RAW_SOCKET]->canMessageDecoderMethod.size()
                    : 0 );
            std::string obdPids = std::to_string(
                ( ( decoderDictionaryMap.find( VehicleDataSourceProtocol::OBD ) != decoderDictionaryMap.end() ) &&
                  ( decoderDictionaryMap[VehicleDataSourceProtocol::OBD] != nullptr ) &&
                  ( !decoderDictionaryMap[VehicleDataSourceProtocol::OBD]->canMessageDecoderMethod.empty() ) )
                    ? decoderDictionaryMap[VehicleDataSourceProtocol::OBD]
                          ->canMessageDecoderMethod.cbegin()
                          ->second.size()
                    : 0 );
            FWE_LOG_INFO( "FWE activated Decoder Manifest:" + std::string( " using decoder manifest:" ) +
                          collectionSchemeManager->mCurrentDecoderManifestID + " resulting in decoding rules for " +
                          std::to_string( decoderDictionaryMap.size() ) +
                          " protocols. Decoder CAN channels: " + decoderCanChannels + " and OBD PIDs:" + obdPids );
            std::string enabled;
            std::string idle;
            collectionSchemeManager->printExistingCollectionSchemes( enabled, idle );
            // coverity[check_return : SUPPRESS]
            FWE_LOG_INFO( "FWE activated collection schemes:" + enabled + " using decoder manifest:" +
                          collectionSchemeManager->mCurrentDecoderManifestID + " resulting in " +
                          std::to_string( inspectionMatrixOutput->conditions.size() ) + " inspection conditions" );
            TraceModule::get().sectionEnd( TraceSection::MANAGER_EXTRACTION );
        }
        /*
         * get next timePoint from the minHeap top
         * check if it is a valid timePoint, it can be obsoleted if start Time or stop Time gets updated
         * It should be always valid because Checkin is default to be running all the time
         */
        auto currentMonotonicTime = collectionSchemeManager->mClock->monotonicTimeSinceEpochMs();
        if ( collectionSchemeManager->mTimeLine.empty() )
        {
            collectionSchemeManager->mWait.wait( Platform::Linux::Signal::WaitWithPredicate );
        }
        else if ( currentMonotonicTime >= collectionSchemeManager->mTimeLine.top().time.monotonicTimeMs )
        {
            // Next checkin time has already expired
        }
        else
        {
            uint32_t waitTimeMs = static_cast<uint32_t>( collectionSchemeManager->mTimeLine.top().time.monotonicTimeMs -
                                                         currentMonotonicTime );
            FWE_LOG_TRACE( "Going to wait for " + std::to_string( waitTimeMs ) + " ms" );
            collectionSchemeManager->mWait.wait( waitTimeMs );
        }
        /* now it is either timer expires, an update arrives from PI, or stop() is called */
        collectionSchemeManager->updateAvailable();
        std::string wakeupStr;
        collectionSchemeManager->printWakeupStatus( wakeupStr );
        FWE_LOG_TRACE( wakeupStr );
        if ( collectionSchemeManager->shouldStop() )
        {
            break;
        }
    }
}

/* callback function */
void
CollectionSchemeManager::onCollectionSchemeUpdate( const ICollectionSchemeListPtr &collectionSchemeList )
{
    std::lock_guard<std::mutex> lock( mSchemaUpdateMutex );
    mCollectionSchemeListInput = collectionSchemeList;
    mCollectionSchemeAvailable = true;
    mWait.notify();
}

void
CollectionSchemeManager::onDecoderManifestUpdate( const IDecoderManifestPtr &decoderManifest )
{
    std::lock_guard<std::mutex> lock( mSchemaUpdateMutex );
    mDecoderManifestInput = decoderManifest;
    mDecoderManifestAvailable = true;
    mWait.notify();
}

void
CollectionSchemeManager::updateAvailable()
{
    std::lock_guard<std::mutex> lock( mSchemaUpdateMutex );
    if ( mCollectionSchemeAvailable && mCollectionSchemeListInput != nullptr )
    {
        mCollectionSchemeList = mCollectionSchemeListInput;
        mProcessCollectionScheme = true;
    }
    mCollectionSchemeAvailable = false;
    if ( mDecoderManifestAvailable && mDecoderManifestInput != nullptr )
    {
        mDecoderManifest = mDecoderManifestInput;
        mProcessDecoderManifest = true;
    }
    mDecoderManifestAvailable = false;
}

/*
 * checkinIntervalMsec - checkin interval in ms
 * debounceIntervalMsec - debounce Interval in ms
 */
bool
CollectionSchemeManager::init( uint32_t checkinIntervalMsec,
                               const std::shared_ptr<ICacheAndPersist> &schemaPersistencyPtr,
                               CANInterfaceIDTranslator &canIDTranslator )
{
    mCANIDTranslator = canIDTranslator;
    FWE_LOG_TRACE( "CollectionSchemeManager initialised with a checkin interval of: " +
                   std::to_string( checkinIntervalMsec ) + " ms" );
    if ( checkinIntervalMsec > 0 )
    {
        mCheckinIntervalInMsec = checkinIntervalMsec;
    }
    else
    {
        /* use default value when checkin interval is not set in configuration */
        mCheckinIntervalInMsec = DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND;
    }
    mSchemaPersistency = schemaPersistencyPtr;
    return true;
}

bool
CollectionSchemeManager::connect()
{
    return start();
}

bool
CollectionSchemeManager::disconnect()
{
    return stop();
}

bool
CollectionSchemeManager::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

bool
CollectionSchemeManager::isCollectionSchemeLoaded()
{
    return ( ( !mEnabledCollectionSchemeMap.empty() ) || ( !mIdleCollectionSchemeMap.empty() ) );
}

/*
 * This function starts from protobuf-ed decodermanifest, and
 * runs through the following steps:
 * a. build decodermanifest
 * b. check if decodermanifest changes. Change of decodermanifest invokes
 * cleanup of collectionSchemeMaps.
 *
 * returns true when mEnabledCollectionSchemeMap changes.
 */
bool
CollectionSchemeManager::processDecoderManifest()
{
    if ( ( mDecoderManifest == nullptr ) || ( !mDecoderManifest->build() ) )
    {
        FWE_LOG_ERROR( " Failed to process the upcoming DecoderManifest." );
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
        return false;
    }
    // build is successful
    if ( mDecoderManifest->getID() == mCurrentDecoderManifestID )
    {
        FWE_LOG_TRACE( "Ignoring new decoder manifest with same name: " + mCurrentDecoderManifestID );
        // no change in decoder manifest
        return false;
    }
    FWE_LOG_TRACE( "Replace decoder manifest " + mCurrentDecoderManifestID + " with " + mDecoderManifest->getID() +
                   " while " + std::to_string( mEnabledCollectionSchemeMap.size() ) + " active and " +
                   std::to_string( mIdleCollectionSchemeMap.size() ) + " idle collection schemes loaded" );
    // store the new DM, update mCurrentDecoderManifestID
    mCurrentDecoderManifestID = mDecoderManifest->getID();
    store( DataType::DECODER_MANIFEST );
    // when DM changes, check if we have collectionScheme loaded
    if ( isCollectionSchemeLoaded() )
    {
        // DM has changes, all existing collectionSchemes need to be cleared
        cleanupCollectionSchemes();
        return false;
    }
    else
    {
        // collectionScheme maps are empty
        return rebuildMapsandTimeLine( mClock->timeSinceEpoch() );
    }
}

/*
 * This function start from protobuf-ed collectionSchemeList
 * runs through the following steps:
 * build collectionSchemeList
 * rebuild or update existing collectionScheme maps when needed
 *
 * returns true when enabledCollectionSchemeMap has changed
 */
bool
CollectionSchemeManager::processCollectionScheme()
{
    if ( ( mCollectionSchemeList == nullptr ) || ( !mCollectionSchemeList->build() ) )
    {
        FWE_LOG_ERROR( "Incoming CollectionScheme does not exist or fails to build!" );
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::COLLECTION_SCHEME_ERROR );
        return false;
    }
    // Build is successful. Store collectionScheme
    store( DataType::COLLECTION_SCHEME_LIST );
    if ( isCollectionSchemeLoaded() )
    {
        // there are existing collectionSchemes, try to update the existing one
        return updateMapsandTimeLine( mClock->timeSinceEpoch() );
    }
    else
    {
        // collectionScheme maps are empty
        return rebuildMapsandTimeLine( mClock->timeSinceEpoch() );
    }
}

TimePoint
CollectionSchemeManager::calculateMonotonicTime( const TimePoint &currTime, Timestamp systemTimeMs )
{
    TimePoint convertedTime = timePointFromSystemTime( currTime, systemTimeMs );
    if ( ( convertedTime.systemTimeMs == 0 ) && ( convertedTime.monotonicTimeMs == 0 ) )
    {
        FWE_LOG_ERROR( "The system time " + std::to_string( systemTimeMs ) +
                       " corresponds to a time in the past before the monotonic" +
                       " clock started ticking. Current system time: " + std::to_string( currTime.systemTimeMs ) +
                       ". Current monotonic time: " + std::to_string( currTime.monotonicTimeMs ) );
        return TimePoint{ systemTimeMs, 0 };
    }
    return convertedTime;
}

/*
 * This function rebuild enableCollectionScheme map, idle collectionScheme map, and timeline.
 * In case a collectionScheme needs to start immediately, this function builds mEnableCollectionSchemeMap and returns
 * true. Otherwise, it returns false.
 */
bool
CollectionSchemeManager::rebuildMapsandTimeLine( const TimePoint &currTime )
{
    bool ret = false;
    std::vector<ICollectionSchemePtr> collectionSchemeList;

    if ( mCollectionSchemeList == nullptr )
    {
        return false;
    }
    collectionSchemeList = mCollectionSchemeList->getCollectionSchemes();
    /* Separate collectionSchemes into Enabled and Idle bucket */
    for ( auto const &collectionScheme : collectionSchemeList )
    {
        if ( collectionScheme->getDecoderManifestID() != mCurrentDecoderManifestID )
        {
            // Encounters a collectionScheme that does not have matching DM
            // Rebuild has to bail out. Call cleanupCollectionSchemes() before exiting.
            FWE_LOG_TRACE(
                "CollectionScheme does not have matching DM ID. Current DM ID: " + mCurrentDecoderManifestID +
                " but collection scheme " + collectionScheme->getCollectionSchemeID() + " needs " +
                collectionScheme->getDecoderManifestID() );

            cleanupCollectionSchemes();
            return false;
        }
        // collectionScheme does not have matching DM, can't rebuild. Exit
        Timestamp startTime = collectionScheme->getStartTime();
        Timestamp stopTime = collectionScheme->getExpiryTime();
        std::string id = collectionScheme->getCollectionSchemeID();
        if ( startTime > currTime.systemTimeMs )
        {
            /* for idleCollectionSchemes, push both startTime and stopTime to timeLine */
            mIdleCollectionSchemeMap[id] = collectionScheme;
            mTimeLine.push( { calculateMonotonicTime( currTime, startTime ), id } );
            mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
        }
        else if ( stopTime > currTime.systemTimeMs )
        { /* At rebuild, if a collectionScheme's startTime has already passed, enable collectionScheme immediately
           */
            mEnabledCollectionSchemeMap[id] = collectionScheme;
            mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
            ret = true;
        }
    }
    std::string enableStr;
    std::string idleStr;
    printExistingCollectionSchemes( enableStr, idleStr );
    FWE_LOG_TRACE( enableStr + idleStr );
    return ret;
}

std::vector<std::string>
CollectionSchemeManager::getCollectionSchemeArns()
{
    std::lock_guard<std::mutex> lock( mSchemaUpdateMutex );
    std::vector<std::string> collectionSchemeArns;
    if ( mCollectionSchemeList != nullptr )
    {
        for ( auto &collectionScheme : mCollectionSchemeList->getCollectionSchemes() )
        {
            collectionSchemeArns.push_back( collectionScheme->getCollectionSchemeID() );
        }
    }
    return collectionSchemeArns;
}

/*
 * This function goes through collectionSchemeList and updates mIdleCollectionSchemeMap, mEnabledCollectionSchemeMap
 * and mTimeLine;
 * For each collectionScheme,
 * If it is enabled, check new StopTime and update mEnabledCollectionSchemeMap, mTimeline and flag Changed when needed;
 * Else
 * Update mIdleCollectionSchemeMap and mTimeLine when needed;
 *
 * Returns true when mEnabledCollectionSchemeMap changes.
 */
bool
CollectionSchemeManager::updateMapsandTimeLine( const TimePoint &currTime )
{
    bool ret = false;
    std::unordered_set<std::string> newCollectionSchemeIDs;
    std::vector<ICollectionSchemePtr> collectionSchemeList;

    if ( mCollectionSchemeList == nullptr )
    {
        return false;
    }
    collectionSchemeList = mCollectionSchemeList->getCollectionSchemes();
    for ( auto const &collectionScheme : collectionSchemeList )
    {
        if ( collectionScheme->getDecoderManifestID() != mCurrentDecoderManifestID )
        {
            // Encounters a collectionScheme that does not have matching DM
            // Rebuild has to bail out. Call cleanupCollectionSchemes() before exiting.
            FWE_LOG_TRACE( "CollectionScheme does not have matching DM ID: " + mCurrentDecoderManifestID + " " +
                           collectionScheme->getDecoderManifestID() );

            cleanupCollectionSchemes();
            return false;
        }
        /*
         * Once collectionScheme has a matching DM, try to locate the collectionScheme in existing maps
         * using collectionScheme ID.
         * If neither found in Enabled nor Idle maps, it is a new collectionScheme, and add it
         * to either enabled map (the collectionScheme might be already overdue due to some other delay
         * in delivering to FWE), or idle map( this is the usual case).
         *
         * If found in enabled map, this is an update to existing collectionScheme, since it is already
         * enabled, just go ahead update expiry time or, disable the collectionScheme if it is due to stop
         * since it is already enabled, just go ahead update expiry time;
         * If found in idle map, just go ahead update the start or stop time in case of any change,
         *  and also check if it is to be enabled immediately.
         *
         */
        Timestamp startTime = collectionScheme->getStartTime();
        Timestamp stopTime = collectionScheme->getExpiryTime();

        std::string id = collectionScheme->getCollectionSchemeID();
        newCollectionSchemeIDs.insert( id );
        auto itEnabled = mEnabledCollectionSchemeMap.find( id );
        auto itIdle = mIdleCollectionSchemeMap.find( id );
        if ( itEnabled != mEnabledCollectionSchemeMap.end() )
        {
            /* found collectionScheme in Enabled map. this collectionScheme is running, check for StopTime only */
            ICollectionSchemePtr currCollectionScheme = itEnabled->second;
            if ( stopTime <= currTime.systemTimeMs )
            {
                /* This collectionScheme needs to stop immediately */
                mEnabledCollectionSchemeMap.erase( id );
                ret = true;
                std::string completedStr;
                completedStr = "Stopping enabled CollectionScheme: ";
                printEventLogMsg( completedStr, id, startTime, stopTime, currTime );
                FWE_LOG_TRACE( completedStr );
            }
            else if ( stopTime != currCollectionScheme->getExpiryTime() )
            {
                /* StopTime changes on that collectionScheme, update with new CollectionScheme */
                mEnabledCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
            }
        }
        else if ( itIdle != mIdleCollectionSchemeMap.end() )
        {
            /* found in Idle map, need to check both StartTime and StopTime */
            ICollectionSchemePtr currCollectionScheme = itIdle->second;
            if ( ( startTime <= currTime.systemTimeMs ) && ( stopTime > currTime.systemTimeMs ) )
            {
                /* this collectionScheme needs to start immediately */
                mIdleCollectionSchemeMap.erase( id );
                mEnabledCollectionSchemeMap[id] = collectionScheme;
                ret = true;
                mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
                std::string startStr;
                startStr = "Starting idle collectionScheme now: ";
                printEventLogMsg( startStr, id, startTime, stopTime, currTime );
                FWE_LOG_TRACE( startStr );
            }
            else if ( ( startTime > currTime.systemTimeMs ) &&
                      ( ( startTime != currCollectionScheme->getStartTime() ) ||
                        ( stopTime != currCollectionScheme->getExpiryTime() ) ) )
            {
                // this collectionScheme is an idle collectionScheme, and its startTime or ExpiryTime
                // or both need updated
                mIdleCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( { calculateMonotonicTime( currTime, startTime ), id } );
                mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
            }
        }
        else
        {
            /*
             * This is a new collectionScheme, might need to activate immediately if startTime has passed
             * Otherwise, add it to idleMap
             */
            std::string addStr;
            addStr = "Adding new collectionScheme: ";
            printEventLogMsg( addStr, id, startTime, stopTime, currTime );
            FWE_LOG_TRACE( addStr );
            if ( ( startTime <= currTime.systemTimeMs ) && ( stopTime > currTime.systemTimeMs ) )
            {
                mEnabledCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
                ret = true;
            }
            else if ( startTime > currTime.systemTimeMs )
            {
                mIdleCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( { calculateMonotonicTime( currTime, startTime ), id } );
                mTimeLine.push( { calculateMonotonicTime( currTime, stopTime ), id } );
            }
        }
    }
    /* Check in newCollectionSchemeIDs set, if any Idle collectionScheme is missing from the set*/
    std::string removeStr;
    auto it = mIdleCollectionSchemeMap.begin();
    while ( it != mIdleCollectionSchemeMap.end() )
    {
        if ( newCollectionSchemeIDs.find( it->first ) == newCollectionSchemeIDs.end() )
        {
            removeStr += it->first;
            it = mIdleCollectionSchemeMap.erase( it );
        }
        else
        {
            it++;
        }
    }
    /* Check in newCollectionSchemeIDs set, if any enabled collectionScheme is missing from the set*/
    it = mEnabledCollectionSchemeMap.begin();
    while ( it != mEnabledCollectionSchemeMap.end() )
    {
        if ( newCollectionSchemeIDs.find( it->first ) == newCollectionSchemeIDs.end() )
        {
            removeStr += it->first;
            it = mEnabledCollectionSchemeMap.erase( it );
            ret = true;
        }
        else
        {
            it++;
        }
    }
    if ( !removeStr.empty() )
    {
        FWE_LOG_TRACE( "Removing collectionSchemes missing from PI updates: " + removeStr );
    }
    std::string enableStr;
    std::string idleStr;
    printExistingCollectionSchemes( enableStr, idleStr );
    FWE_LOG_TRACE( enableStr + idleStr );
    return ret;
}

/*
 * This function checks timeline,
 * 1. Timer has not expired but main thread wakes up because of PI updates,
 * this function always checks if it is a timer expiration first.
 * If not, simply exit, return false;
 * 2. Otherwise,
 *      get topTime and topCollectionSchemeID from top of MINheap,
 *      if it is checkin event, simply send out checkin, this is false case;
 *      if collectionScheme in Enabled Map, and stopTime equal to topTime, time to disable this collectionScheme, this
 * is a true case; else if CollectionScheme in idle map, and startTime equals to topTime, time to enable this
 * collectionScheme, this is a true case; for the rest of the cases, all false;
 *
 * 3. search for the next valid timePoint to set up timer;
 *  Because client may update existing collectionSchemes with new start and stop time, the timepoint
 *  in mTimeline needs to be scanned to make sure next timer is set up for a valid collectionScheme at
 *  a valid time.
 *  The will save us from waking up at an obsolete timePoint and waste context switch.
 *
 * returns true when enabled map changes;
 */
bool
CollectionSchemeManager::checkTimeLine( const TimePoint &currTime )
{
    bool ret = false;
    if ( ( mTimeLine.empty() ) || ( currTime.monotonicTimeMs < mTimeLine.top().time.monotonicTimeMs ) )
    {
        // Timer has not expired, do nothing
        return ret;
    }
    while ( !mTimeLine.empty() )
    {
        const auto &topPair = mTimeLine.top();
        const std::string &topCollectionSchemeID = topPair.id;
        const TimePoint &topTime = topPair.time;
        if ( topCollectionSchemeID == CHECKIN )
        {
            // for checkin, we are about to
            // either serve current checkin event, and move on to search for next timePoint to set up timer;
            // or we find current checkin for setting up next timer, then we are done here;
            if ( currTime.monotonicTimeMs < topTime.monotonicTimeMs )
            {
                // Successfully locate next checkin as timePoint to set up timer
                // time to exit
                break;
            }
            // Try to send the checkin message.
            // If it succeeds, we will schedule the next checkin cycle using the provided interval.
            // If it does not succeed ( e.g. no offboardconnectivity), we schedule for a retry immediately.
            bool checkinSuccess = sendCheckin();
            // Now schedule based on the return code
            if ( checkinSuccess )
            {
                if ( mCheckinIntervalInMsec > 0 )
                {
                    TimePoint nextCheckinTime = { currTime.systemTimeMs + mCheckinIntervalInMsec,
                                                  currTime.monotonicTimeMs + mCheckinIntervalInMsec };
                    mTimeLine.push( { nextCheckinTime, CHECKIN } );
                }
                // else, no checkin message is scheduled.
            }
            else
            {
                // Schedule with for a quick retry
                // Calculate the minimum retry interval
                uint64_t minimumCheckinInterval =
                    std::min( static_cast<uint64_t>( RETRY_CHECKIN_INTERVAL_IN_MILLISECOND ), mCheckinIntervalInMsec );
                TimePoint nextCheckinTime = { currTime.systemTimeMs + minimumCheckinInterval,
                                              currTime.monotonicTimeMs + minimumCheckinInterval };
                mTimeLine.push( { nextCheckinTime, CHECKIN } );
            }

            // after sending checkin, the work on this dataPair is done, move to next dataPair
            // to look for next valid timePoint to set up timer
            mTimeLine.pop();
            continue;
        }

        // in case of non-checkin
        // first find topCollectionSchemeID in mEnabledCollectionSchemeMap then mIdleCollectionSchemeMap
        // if we find a match in collectionScheme ID, check further if topTime matches this collectionScheme's
        // start/stop time
        bool foundInEnabled = true;
        auto it = mEnabledCollectionSchemeMap.find( topCollectionSchemeID );
        if ( it == mEnabledCollectionSchemeMap.end() )
        {
            it = mIdleCollectionSchemeMap.find( topCollectionSchemeID );
            if ( it == mIdleCollectionSchemeMap.end() )
            {
                // Could not find it in Enabled map nor in Idle map,
                // this collectionScheme must have been disabled earlier per
                // client request, this dataPair is obsolete, just drop it
                // keep searching for next valid TimePoint
                // to set up timer
                FWE_LOG_TRACE( "CollectionScheme not found: " + topCollectionSchemeID );
                mTimeLine.pop();
                continue;
            }
            foundInEnabled = false;
        }
        // found it, continue examining topTime
        ICollectionSchemePtr currCollectionScheme;
        Timestamp timeOfInterest = 0ULL;
        if ( foundInEnabled )
        {
            // This collectionScheme is found in mEnabledCollectionSchemeMap
            // topCollectionSchemeID has been enabled, check if stop time matches
            currCollectionScheme = mEnabledCollectionSchemeMap[topCollectionSchemeID];
            timeOfInterest = currCollectionScheme->getExpiryTime();
        }
        else
        {
            // This collectionScheme is found in mIdleCollectionSchemeMap
            // topCollectionSchemeID has not been enabled, check if start time matches
            currCollectionScheme = mIdleCollectionSchemeMap[topCollectionSchemeID];
            timeOfInterest = currCollectionScheme->getStartTime();
        }
        if ( timeOfInterest != topTime.systemTimeMs )
        {
            // this dataPair has a valid collectionScheme ID, but the start time or stop time is already updated
            // not equal to topTime any more; This is an obsolete dataPair. Simply drop it and move on
            // to next pair
            FWE_LOG_TRACE( "Found collectionScheme: " + topCollectionSchemeID +
                           " but time does not match: "
                           "topTime " +
                           std::to_string( topTime.systemTimeMs ) + " timeFromCollectionScheme " +
                           std::to_string( timeOfInterest ) );
            mTimeLine.pop();
            continue;
        }
        // now we have a dataPair with valid collectionScheme ID, and valid start/stop time
        // Check if it is time to enable/disable this collectionScheme, or else
        // topTime is far down the timeline, it is a timePoint to set up next timer.
        if ( topTime.monotonicTimeMs <= currTime.monotonicTimeMs )
        {
            ret = true;
            // it is time to enable or disable this collectionScheme
            if ( !foundInEnabled )
            {
                // enable the collectionScheme
                mEnabledCollectionSchemeMap[topCollectionSchemeID] = currCollectionScheme;
                mIdleCollectionSchemeMap.erase( topCollectionSchemeID );
                std::string enableStr;
                enableStr = "Enabling idle collectionScheme: ";
                printEventLogMsg( enableStr,
                                  topCollectionSchemeID,
                                  currCollectionScheme->getStartTime(),
                                  currCollectionScheme->getExpiryTime(),
                                  topTime );
                FWE_LOG_INFO( enableStr );
            }
            else
            {
                // disable the collectionScheme
                std::string disableStr;
                disableStr = "Disabling enabled collectionScheme: ";
                printEventLogMsg( disableStr,
                                  topCollectionSchemeID,
                                  currCollectionScheme->getStartTime(),
                                  currCollectionScheme->getExpiryTime(),
                                  topTime );
                FWE_LOG_INFO( disableStr );
                mEnabledCollectionSchemeMap.erase( topCollectionSchemeID );
            }
        }
        else
        {
            // Successfully locate the next valid TimePoint
            // stop searching
            break;
        }
        // continue searching for next valid timepoint to set up timer
        mTimeLine.pop();
    }
    if ( !mTimeLine.empty() )
    {
        FWE_LOG_TRACE( "Top pair: " + std::to_string( mTimeLine.top().time.monotonicTimeMs ) + " " +
                       mTimeLine.top().id + " currTime: " + std::to_string( currTime.monotonicTimeMs ) );
    }
    return ret;
}
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
