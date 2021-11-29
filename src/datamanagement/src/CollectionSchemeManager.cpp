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

// Includes
#include "CollectionSchemeManager.h"
#include "CollectionSchemeIngestionList.h"
#include "DecoderManifestIngestion.h"
#include "TraceModule.h"
#include <stack>
#include <string>
#include <unordered_set>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
const std::string CollectionSchemeManager::CHECKIN = "Checkin"; // NOLINT(cert-err58-cpp)

ICollectionSchemeManager::~ICollectionSchemeManager()
{
}

CollectionSchemeManager::CollectionSchemeManager()
    : currentDecoderManifestID( "" )
    , mCheckinIntervalInMsec( DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND )
    , mCollectionSchemeAvailable( false )
    , mDecoderManifestAvailable( false )
    , mProcessCollectionScheme( false )
    , mProcessDecoderManifest( false )
{
}

CollectionSchemeManager::CollectionSchemeManager( const std::string dm_id )
    : currentDecoderManifestID( dm_id )
    , mCheckinIntervalInMsec( DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND )
    , mCollectionSchemeAvailable( false )
    , mDecoderManifestAvailable( false )
    , mProcessCollectionScheme( false )
    , mProcessDecoderManifest( false )
{
}

CollectionSchemeManager::CollectionSchemeManager( const std::string dm_id,
                                                  const std::map<std::string, ICollectionSchemePtr> &mapEnabled,
                                                  const std::map<std::string, ICollectionSchemePtr> &mapIdle )
    : mIdleCollectionSchemeMap( mapIdle )
    , mEnabledCollectionSchemeMap( mapEnabled )
    , currentDecoderManifestID( dm_id )
    , mCheckinIntervalInMsec( DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND )
    , mCollectionSchemeAvailable( false )
    , mDecoderManifestAvailable( false )
    , mProcessCollectionScheme( false )
    , mProcessDecoderManifest( false )
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
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( doWork, this ) )
    {
        mLogger.error( "CollectionSchemeManager::start", " Collection Scheme Thread failed to start " );
    }
    else
    {
        mLogger.info( "CollectionSchemeManager::start", " Collection Scheme Thread started " );
        mThread.setThreadName( "fwDMColSchMngr" );
    }
    return mThread.isValid();
}

bool
CollectionSchemeManager::stop()
{
    std::lock_guard<std::recursive_mutex> lock( mThreadMutex );
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
    mLogger.info( "CollectionSchemeManager::stop", " Collection Scheme Thread stopped " );
    return true;
}

bool
CollectionSchemeManager::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

/* supporting functions for mLogger */
void
CollectionSchemeManager::printEventLogMsg( std::string &msg,
                                           const std::string &id,
                                           const TimePointInMsec &startTime,
                                           const TimePointInMsec &stopTime,
                                           const TimePointInMsec &currTime )
{
    msg += "ID( " + id + " )";
    msg += "Start( " + std::to_string( startTime ) + " milliseconds )";
    msg += "Stop( " + std::to_string( stopTime ) + " milliseconds )";
    msg += "at Current( " + std::to_string( currTime ) + " milliseconds ).";
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
CollectionSchemeManager::printWakeupStatus( std::string &wakeupStr )
{
    wakeupStr = " Waking up to update the CollectionScheme: ";
    wakeupStr += mProcessCollectionScheme ? "Yes." : "No.";
    wakeupStr += " and the DecoderManifest: ";
    wakeupStr += mProcessDecoderManifest ? "Yes." : "No.";
}

void
CollectionSchemeManager::prepareCheckinTimer()
{
    TimePointInMsec currTime = mClock->timeSinceEpochMs();
    TimeData checkinData = std::make_pair( currTime, CHECKIN );
    mTimeLine.push( checkinData );
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
    TimeData saveTimeData = { 0, "" };
    while ( !mTimeLine.empty() )
    {
        if ( mTimeLine.top().second == CHECKIN )
        {
            saveTimeData = mTimeLine.top();
        }
        mTimeLine.pop();
    }
    if ( saveTimeData.first != 0 )
    {
        mTimeLine.push( saveTimeData );
    }
}

bool
CollectionSchemeManager::retrieve( DataType retrieveType )
{
    size_t protoSize = 0;
    ErrorCode ret = SUCCESS;
    std::vector<uint8_t> protoOutput;
    std::string infoStr = "";
    std::string errStr = "";

    if ( mSchemaPersistency == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::retrieve",
                       "Failed to acquire a valid handle on the scheme local persistency module " );
        return false;
    }
    switch ( retrieveType )
    {
    case COLLECTION_SCHEME_LIST:
        infoStr = "Retrieved a CollectionSchemeList of size ";
        errStr = "Failed to retrieve the CollectionSchemeList from the persistency module due to an error :";
        break;
    case DECODER_MANIFEST:
        infoStr = "Retrieved a DecoderManifest of size ";
        errStr = "Failed to retrieve the DecoderManifest from the persistency module due to an error :";
        break;
    default:
        mLogger.error( "CollectionSchemeManager::retrieve", " unknown error : " + std::to_string( retrieveType ) );
        return false;
    }

    protoSize = mSchemaPersistency->getSize( retrieveType );
    if ( protoSize <= 0 )
    {
        mLogger.info( "CollectionSchemeManager::retrieve", infoStr + "zero." );
        return false;
    }
    protoOutput.resize( protoSize );
    ret = mSchemaPersistency->read( protoOutput.data(), protoSize, retrieveType );
    if ( ret != SUCCESS )
    {
        mLogger.error( "CollectionSchemeManager::retrieve", errStr + mSchemaPersistency->getErrorString( ret ) );
        return false;
    }
    mLogger.info( "CollectionSchemeManager::retrieve", infoStr + std::to_string( protoSize ) + " successfully." );
    if ( retrieveType == COLLECTION_SCHEME_LIST )
    {
        // updating mCollectionSchemeList
        if ( mCollectionSchemeList == nullptr )
        {
            mCollectionSchemeList = std::make_shared<CollectionSchemeIngestionList>();
        }
        mCollectionSchemeList->copyData( protoOutput.data(), protoSize );
        mProcessCollectionScheme = true;
    }
    else
    {
        // updating mDecoderManifest
        if ( mDecoderManifest == nullptr )
        {
            mDecoderManifest = std::make_shared<DecoderManifestIngestion>();
        }
        mDecoderManifest->copyData( protoOutput.data(), protoSize );
        mProcessDecoderManifest = true;
    }
    return true;
}

void
CollectionSchemeManager::store( DataType storeType )
{
    ErrorCode ret = SUCCESS;
    std::vector<uint8_t> protoInput;
    std::string logStr;

    if ( mSchemaPersistency == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::store",
                       "Failed to acquire a valid handle on the scheme local persistency module" );
        return;
    }
    if ( storeType == COLLECTION_SCHEME_LIST && mCollectionSchemeList == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::store", "Invalid CollectionSchemeList" );
        return;
    }
    if ( storeType == DECODER_MANIFEST && mDecoderManifest == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::store", "Invalid DecoderManifest" );
        return;
    }
    switch ( storeType )
    {
    case COLLECTION_SCHEME_LIST:
        protoInput = mCollectionSchemeList->getData();
        logStr = "The CollectionSchemeList";
        break;
    case DECODER_MANIFEST:
        protoInput = mDecoderManifest->getData();
        logStr = "The DecoderManifest";
        break;
    default:
        mLogger.error( "CollectionSchemeManager::store",
                       "cannot store unsupported type of " + std::to_string( storeType ) );
        return;
    }

    if ( protoInput.size() <= 0 )
    {
        mLogger.error( "CollectionSchemeManager::store", logStr + " data size is zero." );
        return;
    }
    ret = mSchemaPersistency->write( protoInput.data(), protoInput.size(), storeType );
    if ( ret != SUCCESS )
    {
        mLogger.error( "CollectionSchemeManager::store",
                       "failed to persist " + logStr +
                           " because of this error: " + mSchemaPersistency->getErrorString( ret ) );
    }
    else
    {
        mLogger.trace( "CollectionSchemeManager::store", logStr + " persisted successfully." );
    }
}

/* main thread */
void
CollectionSchemeManager::doWork( void *data )
{
    CollectionSchemeManager *collectionSchemeManager = static_cast<CollectionSchemeManager *>( data );
    bool enabledCollectionSchemeMapChanged = false;

    // set up timer for checkin
    collectionSchemeManager->prepareCheckinTimer();
    // retrieve collectionSchemeList and decoderManifest from persistent storage
    static_cast<void>( collectionSchemeManager->retrieve( COLLECTION_SCHEME_LIST ) );
    static_cast<void>( collectionSchemeManager->retrieve( DECODER_MANIFEST ) );
    do
    {
        if ( collectionSchemeManager->mProcessDecoderManifest )
        {
            collectionSchemeManager->mProcessDecoderManifest = false;
            TraceModule::get().sectionBegin( MANAGER_DECODER_BUILD );
            enabledCollectionSchemeMapChanged |= collectionSchemeManager->processDecoderManifest();
            TraceModule::get().sectionEnd( MANAGER_DECODER_BUILD );
        }
        if ( collectionSchemeManager->mProcessCollectionScheme )
        {
            collectionSchemeManager->mProcessCollectionScheme = false;
            TraceModule::get().sectionBegin( MANAGER_COLLECTION_BUILD );
            enabledCollectionSchemeMapChanged |= collectionSchemeManager->processCollectionScheme();
            TraceModule::get().sectionEnd( MANAGER_COLLECTION_BUILD );
        }
        auto checkTime = collectionSchemeManager->mClock->timeSinceEpochMs();
        enabledCollectionSchemeMapChanged |= collectionSchemeManager->checkTimeLine( checkTime );
        if ( enabledCollectionSchemeMapChanged )
        {
            TraceModule::get().sectionBegin( MANAGER_EXTRACTION );
            collectionSchemeManager->mLogger.trace(
                "CollectionSchemeManager::doWork",
                "Start extraction because of changed active collection schemes at time " +
                    std::to_string( checkTime ) );
            /*
             * Extract InspectionMatrix from mEnabledCollectionSchemeMap
             *
             * input: mEnabledCollectionSchemeMap
             * output: shared_ptr to InspectionMatrix
             *
             * Then, propagate inspection matrix to Inspection engine
             */
            enabledCollectionSchemeMapChanged = false;
            std::shared_ptr<InspectionMatrix> inspectionMatrixOutput = std::make_shared<InspectionMatrix>();
            collectionSchemeManager->inspectionMatrixExtractor( inspectionMatrixOutput );
            collectionSchemeManager->inspectionMatrixUpdater( inspectionMatrixOutput );
            /*
             * extract decoder dictionary
             * input: mDecoderManifest
             * output: shared_ptr to decoderDictionary
             *
             * the propagate the output to Network Channel Consumers
             */
            std::map<NetworkChannelProtocol, std::shared_ptr<CANDecoderDictionary>> decoderDictionaryMap;
            collectionSchemeManager->decoderDictionaryExtractor( decoderDictionaryMap );
            // Publish decoder dictionaries update to all listeners
            collectionSchemeManager->decoderDictionaryUpdater( decoderDictionaryMap );

            std::string canInfo = "";
            std::string enabled = "";
            std::string idle = "";
            collectionSchemeManager->printExistingCollectionSchemes( enabled, idle );
            // coverity[check_return : SUPPRESS]
            collectionSchemeManager->mLogger.info(
                "CollectionSchemeManager::doWork",
                "FWE activated collection schemes:" + enabled +
                    " using decoder manifest:" + collectionSchemeManager->currentDecoderManifestID + " resulting in " +
                    std::to_string( inspectionMatrixOutput->conditions.size() ) +
                    " inspection conditions and decoding rules for " + std::to_string( decoderDictionaryMap.size() ) +
                    " protocols. Decoder CAN channels: " +
                    std::to_string( ( decoderDictionaryMap.find( RAW_SOCKET ) != decoderDictionaryMap.end() &&
                                      decoderDictionaryMap[RAW_SOCKET] != nullptr )
                                        ? decoderDictionaryMap[RAW_SOCKET]->canMessageDecoderMethod.size()
                                        : 0 ) +
                    " and OBD PIDs:" +
                    std::to_string( ( decoderDictionaryMap.find( OBD ) != decoderDictionaryMap.end() &&
                                      decoderDictionaryMap[OBD] != nullptr &&
                                      decoderDictionaryMap[OBD]->canMessageDecoderMethod.size() > 0 )
                                        ? decoderDictionaryMap[OBD]->canMessageDecoderMethod.cbegin()->second.size()
                                        : 0 ) );
            TraceModule::get().sectionEnd( MANAGER_EXTRACTION );
        }
        /*
         * get next timePoint from the minHeap top
         * check if it is a valid timePoint, it can be obsoleted if start Time or stop Time gets updated
         * It should be always valid because Checkin is default to be running all the time
         */
        auto currentTime = collectionSchemeManager->mClock->timeSinceEpochMs();
        if ( collectionSchemeManager->mTimeLine.empty() )
        {
            collectionSchemeManager->mWait.wait( Platform::Signal::WaitWithPredicate );
        }
        else if ( currentTime >= collectionSchemeManager->mTimeLine.top().first )
        {
            // Next checkin time has already expired
        }
        else
        {
            uint32_t waitTime = static_cast<uint32_t>( collectionSchemeManager->mTimeLine.top().first - currentTime );
            collectionSchemeManager->mWait.wait( waitTime );
        }
        /* now it is either timer expires, an update arrives from PI, or stop() is called */
        collectionSchemeManager->updateAvailable();
        std::string wakeupStr;
        collectionSchemeManager->printWakeupStatus( wakeupStr );
        collectionSchemeManager->mLogger.trace( "CollectionSchemeManager::doWork", wakeupStr );
    } while ( !collectionSchemeManager->shouldStop() );
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
    mLogger.trace( "CollectionSchemeManager::init",
                   "CollectionSchemeManager initialised with a checkin interval of : " +
                       std::to_string( checkinIntervalMsec ) + " ms." );
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
    return ( !mEnabledCollectionSchemeMap.empty() || !mIdleCollectionSchemeMap.empty() );
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
    if ( mDecoderManifest == nullptr || !mDecoderManifest->build() )
    {
        mLogger.error( "CollectionSchemeManager::processDecoderManifest",
                       " Failed to process the upcoming DecoderManifest." );
        return false;
    }
    // build is successful
    if ( mDecoderManifest->getID() == currentDecoderManifestID )
    {
        mLogger.trace( "CollectionSchemeManager::processDecoderManifest",
                       "Ignoring new decoder manifest with same name: " + currentDecoderManifestID );
        // no change in decoder manifest
        return false;
    }
    mLogger.trace( "CollectionSchemeManager::processDecoderManifest",
                   "Replace decoder manifest " + currentDecoderManifestID + " with " + mDecoderManifest->getID() +
                       " while " + std::to_string( mEnabledCollectionSchemeMap.size() ) + " active and " +
                       std::to_string( mIdleCollectionSchemeMap.size() ) + " idle collection schemes loaded" );
    // store the new DM, update currentDecoderManifestID
    currentDecoderManifestID = mDecoderManifest->getID();
    store( DECODER_MANIFEST );
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
        return rebuildMapsandTimeLine( mClock->timeSinceEpochMs() );
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
    if ( mCollectionSchemeList == nullptr || !mCollectionSchemeList->build() )
    {
        mLogger.error( "CollectionSchemeManager::processCollectionScheme",
                       "Incoming CollectionScheme does not exist or fails to build!" );
        return false;
    }
    // Build is successful. Store collectionScheme
    store( COLLECTION_SCHEME_LIST );
    if ( isCollectionSchemeLoaded() )
    {
        // there are existing collectionSchemes, try to update the existing one
        return updateMapsandTimeLine( mClock->timeSinceEpochMs() );
    }
    else
    {
        // collectionScheme maps are empty
        return rebuildMapsandTimeLine( mClock->timeSinceEpochMs() );
    }
}

void
CollectionSchemeManager::decoderDictionaryExtractor(
    std::map<NetworkChannelProtocol, std::shared_ptr<CANDecoderDictionary>> &decoderDictionaryMap )
{
    // Iterate through enabled collectionScheme lists to locate the signals and CAN frames to be collected
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); ++it )
    {
        const auto &collectionSchemePtr = it->second;
        // first iterate through the signalID lists
        for ( const auto &signalInfo : collectionSchemePtr->getCollectSignals() )
        {
            // get the Network Protocol Type: CAN, OBD, SOMEIP, etc
            auto networkType = mDecoderManifest->getNetworkProtocol( signalInfo.signalID );
            if ( networkType == INVALID_PROTOCOL )
            {
                mLogger.warn( "CollectionSchemeManager::decoderDictionaryExtractor",
                              "Invalid protocol provided for signal : " + std::to_string( signalInfo.signalID ) );
                // This signal contains invalid network protocol, cannot include it onto decoder dictionary
                continue;
            }
            // Firstly we need to check if we already have dictionary created for this network
            if ( decoderDictionaryMap.find( networkType ) == decoderDictionaryMap.end() )
            {
                // Currently we don't have decoder dictionary for this type of network protocol, create one
                decoderDictionaryMap.emplace( networkType, std::make_shared<CANDecoderDictionary>() );
            }

            if ( networkType == RAW_SOCKET )
            {
                auto canRawFrameID = mDecoderManifest->getCANFrameAndInterfaceID( signalInfo.signalID ).first;
                auto interfaceId = mDecoderManifest->getCANFrameAndInterfaceID( signalInfo.signalID ).second;

                auto canChannelID = mCANIDTranslator.getChannelNumericID( interfaceId );
                if ( canChannelID == INVALID_CAN_CHANNEL_NUMERIC_ID )
                {
                    mLogger.warn( "CollectionSchemeManager::decoderDictionaryExtractor",
                                  "Invalid Interface ID provided: " + interfaceId );
                }
                else
                {
                    auto &canDecoderDictionaryPtr = decoderDictionaryMap[networkType];
                    // Add signalID to the set of this decoder dictionary
                    canDecoderDictionaryPtr->signalIDsToCollect.insert( signalInfo.signalID );
                    // firstly check if we have canChannelID entry at dictionary top layer
                    if ( canDecoderDictionaryPtr->canMessageDecoderMethod.find( canChannelID ) ==
                         canDecoderDictionaryPtr->canMessageDecoderMethod.end() )
                    {
                        // create an entry for canChannelID if it's not existed yet
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID] =
                            std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>();
                    }
                    // check if this CAN Frame already exits in dictionary, if so, update if its a raw can decoder
                    // method.
                    // If not, we need to create an entry for this CAN Frame which will include decoder
                    // format for all signals defined in decoder manifest
                    if ( canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].find( canRawFrameID ) ==
                         canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].end() )
                    {
                        CANMessageDecoderMethod decoderMethod;
                        // We set the collect Type to DECODE at this stage. In the second half of this function, we will
                        // examine the CAN Frames. If there's any CAN Frame to have both signal and raw bytes to be
                        // collected, the type will be updated to RAW_AND_DECODE
                        decoderMethod.collectType = CANMessageCollectType::DECODE;
                        decoderMethod.format = mDecoderManifest->getCANMessageFormat( canRawFrameID, interfaceId );
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID] = decoderMethod;
                    }
                    else if ( canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID]
                                  .collectType == CANMessageCollectType::RAW )
                    {
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID].collectType =
                            CANMessageCollectType::RAW_AND_DECODE;
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID].format =
                            mDecoderManifest->getCANMessageFormat( canRawFrameID, interfaceId );
                    }
                }
            }
            else if ( networkType == OBD )
            {
                auto pidDecoderFormat = mDecoderManifest->getPIDSignalDecoderFormat( signalInfo.signalID );
                // There's only one OBD Channel, this is just a place holder to maintain the generic dictionary
                // structure
                CANChannelNumericID canChannelID = 0;
                auto &obdPidCanDecoderDictionaryPtr = decoderDictionaryMap[networkType];
                obdPidCanDecoderDictionaryPtr->signalIDsToCollect.insert( signalInfo.signalID );
                obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.emplace(
                    canChannelID, std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>() );
                if ( obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.find( canChannelID ) ==
                     obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.end() )
                {
                    // create an entry for canChannelID if it's not existed yet
                    obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID] =
                        std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>();
                }
                if ( obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                         .find( pidDecoderFormat.mPID ) ==
                     obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID ).end() )
                {
                    // There's no Dictionary Entry created for this PID yet, create one
                    obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                        .emplace( pidDecoderFormat.mPID, CANMessageDecoderMethod() );
                    obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                        .at( pidDecoderFormat.mPID )
                        .format.mMessageID = pidDecoderFormat.mPID;
                    obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                        .at( pidDecoderFormat.mPID )
                        .format.mSizeInBytes = static_cast<uint8_t>( pidDecoderFormat.mPidResponseLength );
                }
                // Below is the OBD Signal format represented in generic Signal Format
                CANSignalFormat format;
                format.mSignalID = signalInfo.signalID;
                format.mFirstBitPosition =
                    static_cast<uint16_t>( pidDecoderFormat.mStartByte * BYTE_SIZE + pidDecoderFormat.mBitRightShift );
                format.mSizeInBits = static_cast<uint16_t>( ( pidDecoderFormat.mByteLength - 1 ) * BYTE_SIZE +
                                                            pidDecoderFormat.mBitMaskLength );
                format.mFactor = pidDecoderFormat.mScaling;
                format.mOffset = pidDecoderFormat.mOffset;
                obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                    .at( pidDecoderFormat.mPID )
                    .format.mSignals.emplace_back( format );
            }
        }
        // Next let's iterate through the CAN Frames that collectionScheme wants to collect.
        // If some CAN Frame has signals to be decoded, we will set its collectType as RAW_AND_DECODE.
        if ( !collectionSchemePtr->getCollectRawCanFrames().empty() )
        {
            if ( decoderDictionaryMap.find( RAW_SOCKET ) == decoderDictionaryMap.end() )
            {
                // Currently we don't have decoder dictionary for this type of network protocol, create one
                decoderDictionaryMap.emplace( RAW_SOCKET, std::make_shared<CANDecoderDictionary>() );
            }
            auto &canDecoderDictionaryPtr = decoderDictionaryMap[RAW_SOCKET];
            for ( const auto &canFrameInfo : collectionSchemePtr->getCollectRawCanFrames() )
            {
                auto canChannelID = mCANIDTranslator.getChannelNumericID( canFrameInfo.interfaceID );
                if ( canChannelID == INVALID_CAN_CHANNEL_NUMERIC_ID )
                {
                    mLogger.warn( "CollectionSchemeManager::decoderDictionaryExtractor",
                                  "Invalid Interface ID provided:" + canFrameInfo.interfaceID );
                }
                else
                {
                    if ( canDecoderDictionaryPtr->canMessageDecoderMethod.find( canChannelID ) ==
                         canDecoderDictionaryPtr->canMessageDecoderMethod.end() )
                    {
                        // create an entry for canChannelID if the dictionary doesn't have one
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID] =
                            std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>();
                    }
                    // check if we already have entry for CAN Frame. If not, it means this CAN Frame doesn't contain any
                    // Signals to decode, hence the collectType will be RAW only.
                    if ( canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].find( canFrameInfo.frameID ) ==
                         canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].end() )
                    {
                        // there's entry for CANChannelNumericID but no corresponding canFrameID
                        CANMessageDecoderMethod canMessageDecoderMethod;
                        canMessageDecoderMethod.collectType = CANMessageCollectType::RAW;
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canFrameInfo.frameID] =
                            canMessageDecoderMethod;
                    }
                    else
                    {
                        // This CAN Frame contains signal to be decoded. As we need to collect both CAN Frame and
                        // signal, set the collectType as RAW_AND_DECODE
                        canDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                            .at( canFrameInfo.frameID )
                            .collectType = CANMessageCollectType::RAW_AND_DECODE;
                    }
                }
            }
        }
    }
}

void
CollectionSchemeManager::decoderDictionaryUpdater(
    std::map<NetworkChannelProtocol, std::shared_ptr<CANDecoderDictionary>> &decoderDictionaryMap )
{
    for ( auto const &dict : decoderDictionaryMap )
    {
        notifyListeners<const std::shared_ptr<const CANDecoderDictionary> &>(
            &IActiveDecoderDictionaryListener::onChangeOfActiveDictionary, dict.second, dict.first );
    }
}

/*
 * This function rebuild enableCollectionScheme map, idle collectionScheme map, and timeline.
 * In case a collectionScheme needs to start immediately, this function builds mEnableCollectionSchemeMap and returns
 * true. Otherwise, it returns false.
 */
bool
CollectionSchemeManager::rebuildMapsandTimeLine( const TimePointInMsec &currTime )
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
        if ( collectionScheme->getDecoderManifestID() != currentDecoderManifestID )
        {
            // Encounters a collectionScheme that does not have matching DM
            // Rebuild has to bail out. Call cleanupCollectionSchemes() before exiting.
            mLogger.trace( "CollectionSchemeManager::rebuildMapsandTimeLine",
                           "CollectionScheme does not have matching DM ID. Current DM ID: " + currentDecoderManifestID +
                               " but collection scheme " + collectionScheme->getCollectionSchemeID() + " needs " +
                               collectionScheme->getDecoderManifestID() );

            cleanupCollectionSchemes();
            return false;
        }
        // collectionScheme does not have matching DM, can't rebuild. Exit
        TimePointInMsec startTime = collectionScheme->getStartTime();
        TimePointInMsec stopTime = collectionScheme->getExpiryTime();
        std::string id = collectionScheme->getCollectionSchemeID();
        if ( startTime > currTime )
        {
            /* for idleCollectionSchemes, push both startTime and stopTime to timeLine */
            mIdleCollectionSchemeMap[id] = collectionScheme;
            mTimeLine.push( std::make_pair( startTime, id ) );
            mTimeLine.push( std::make_pair( stopTime, id ) );
        }
        else if ( stopTime > currTime )
        { /* At rebuild, if a collectionScheme's startTime has already passed, enable collectionScheme immediately */
            mEnabledCollectionSchemeMap[id] = collectionScheme;
            mTimeLine.push( std::make_pair( stopTime, id ) );
            ret = true;
        }
    }
    std::string enableStr = "";
    std::string idleStr = "";
    printExistingCollectionSchemes( enableStr, idleStr );
    mLogger.trace( "CollectionSchemeManager::rebuildMapsandTimeLine", enableStr + idleStr );
    return ret;
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
CollectionSchemeManager::updateMapsandTimeLine( const TimePointInMsec &currTime )
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
        if ( collectionScheme->getDecoderManifestID() != currentDecoderManifestID )
        {
            // Encounters a collectionScheme that does not have matching DM
            // Rebuild has to bail out. Call cleanupCollectionSchemes() before exiting.
            mLogger.trace( "CollectionSchemeManager::updateMapsandTimeLine",
                           "CollectionScheme does not have matching DM ID: " + currentDecoderManifestID + " " +
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
        TimePointInMsec startTime = collectionScheme->getStartTime();
        TimePointInMsec stopTime = collectionScheme->getExpiryTime();

        std::string id = collectionScheme->getCollectionSchemeID();
        newCollectionSchemeIDs.insert( id );
        auto itEnabled = mEnabledCollectionSchemeMap.find( id );
        auto itIdle = mIdleCollectionSchemeMap.find( id );
        if ( itEnabled != mEnabledCollectionSchemeMap.end() )
        {
            /* found collectionScheme in Enabled map. this collectionScheme is running, check for StopTime only */
            ICollectionSchemePtr currCollectionScheme = itEnabled->second;
            if ( stopTime <= currTime )
            {
                /* This collectionScheme needs to stop immediately */
                mEnabledCollectionSchemeMap.erase( id );
                ret = true;
                std::string completedStr;
                completedStr = "Stopping enabled CollectionScheme: ";
                printEventLogMsg( completedStr, id, startTime, stopTime, currTime );
                mLogger.trace( "collectionSchemeManager::updateMapsandTimeLine ", completedStr );
            }
            else if ( stopTime != currCollectionScheme->getExpiryTime() )
            {
                /* StopTime changes on that collectionScheme, update with new CollectionScheme */
                mEnabledCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( std::make_pair( stopTime, id ) );
            }
        }
        else if ( itIdle != mIdleCollectionSchemeMap.end() )
        {
            /* found in Idle map, need to check both StartTime and StopTime */
            ICollectionSchemePtr currCollectionScheme = itIdle->second;
            if ( startTime <= currTime && stopTime > currTime )
            {
                /* this collectionScheme needs to start immediately */
                mIdleCollectionSchemeMap.erase( id );
                mEnabledCollectionSchemeMap[id] = collectionScheme;
                ret = true;
                mTimeLine.push( std::make_pair( stopTime, id ) );
                std::string startStr;
                startStr = "Starting idle collectionScheme now: ";
                printEventLogMsg( startStr, id, startTime, stopTime, currTime );
                mLogger.trace( "collectionSchemeManager::updateMapsandTimeLine ", startStr );
            }
            else if ( startTime > currTime && ( ( startTime != currCollectionScheme->getStartTime() ) ||
                                                ( stopTime != currCollectionScheme->getExpiryTime() ) ) )
            {
                // this collectionScheme is an idle collectionScheme, and its startTime or ExpiryTime
                // or both need updated
                mIdleCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( make_pair( startTime, id ) );
                mTimeLine.push( make_pair( stopTime, id ) );
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
            mLogger.trace( "collectionSchemeManager::updateMapsandTimeLine ", addStr );
            if ( startTime <= currTime && stopTime > currTime )
            {
                mEnabledCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( std::make_pair( stopTime, id ) );
                ret = true;
            }
            else if ( startTime > currTime )
            {
                mIdleCollectionSchemeMap[id] = collectionScheme;
                mTimeLine.push( std::make_pair( startTime, id ) );
                mTimeLine.push( std::make_pair( stopTime, id ) );
            }
        }
    }
    /* Check in newCollectionSchemeIDs set, if any Idle collectionScheme is missing from the set*/
    std::string removeStr;
    for ( auto it = mIdleCollectionSchemeMap.begin(); it != mIdleCollectionSchemeMap.end(); )
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
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); )
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
        mLogger.trace( "CollectionSchemeManager::updateMapsandTimeLine",
                       "Removing collectionSchemes missing from PI updates: " + removeStr );
    }
    std::string enableStr = "";
    std::string idleStr = "";
    printExistingCollectionSchemes( enableStr, idleStr );
    mLogger.trace( "CollectionSchemeManager::updateMapsandTimeLine", enableStr + idleStr );
    return ret;
}

bool
CollectionSchemeManager::sendCheckin()
{
    // Create a list of active collectionSchemes and the current decoder manifest and send it to cloud
    std::vector<std::string> checkinMsg;
    std::string checkinLogStr;
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); it++ )
    {
        checkinMsg.emplace_back( it->first );
        checkinLogStr += it->first + ' ';
    }
    for ( auto it = mIdleCollectionSchemeMap.begin(); it != mIdleCollectionSchemeMap.end(); it++ )
    {
        checkinMsg.emplace_back( it->first );
        checkinLogStr += it->first + ' ';
    }
    if ( !currentDecoderManifestID.empty() )
    {
        checkinMsg.emplace_back( currentDecoderManifestID );
        checkinLogStr += currentDecoderManifestID;
    }
    mLogger.trace( "CollectionSchemeManager::sendCheckin ", "CHECKIN " + checkinLogStr );

    if ( mSchemaListenerPtr == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::sendCheckin", "Cannot set the checkin message " );
        return false;
    }
    else
    {
        return mSchemaListenerPtr->sendCheckin( checkinMsg );
    }
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
CollectionSchemeManager::checkTimeLine( const TimePointInMsec &currTime )
{
    bool ret = false;
    if ( mTimeLine.empty() || currTime < mTimeLine.top().first )
    {
        // Timer has not expired, do nothing
        return ret;
    }
    while ( !mTimeLine.empty() )
    {
        const auto &topPair = mTimeLine.top();
        const std::string &topCollectionSchemeID = topPair.second;
        const TimePointInMsec &topTime = topPair.first;
        if ( topCollectionSchemeID == CHECKIN )
        {
            // for checkin, we are about to
            // either serve current checkin event, and move on to search for next timePoint to set up timer;
            // or we find current checkin for setting up next timer, then we are done here;
            if ( currTime < topTime )
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
                    TimePointInMsec nextCheckinTime = currTime + mCheckinIntervalInMsec;
                    TimeData newPair = std::make_pair( nextCheckinTime, CHECKIN );
                    mTimeLine.push( newPair );
                }
                // else, no checkin message is scheduled.
            }
            else
            {
                // Schedule with for a quick retry
                // Calculate the minimum retry interval
                uint64_t minimumCheckinInterval =
                    std::min( static_cast<uint64_t>( RETRY_CHECKIN_INTERVAL_IN_MILLISECOND ), mCheckinIntervalInMsec );
                TimePointInMsec nextCheckinTime = currTime + minimumCheckinInterval;
                TimeData newPair = std::make_pair( nextCheckinTime, CHECKIN );
                mTimeLine.push( newPair );
                mLogger.error( "CollectionSchemeManager::checkTimeLine",
                               "The checkin message sending failed. Rescheduling the operation in : " +
                                   std::to_string( minimumCheckinInterval ) + " ms" );
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
                mLogger.trace( "CollectionSchemeManager::checkTimeLine ",
                               "CollectionScheme not found: " + topCollectionSchemeID );
                mTimeLine.pop();
                continue;
            }
            foundInEnabled = false;
        }
        // found it, continue examining topTime
        ICollectionSchemePtr currCollectionScheme;
        TimePointInMsec timeOfInterest = 0ULL;
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
        if ( timeOfInterest != topTime )
        {
            // this dataPair has a valid collectionScheme ID, but the start time or stop time is already updated
            // not equal to topTime any more; This is an obsolete dataPair. Simply drop it and move on
            // to next pair
            mLogger.trace( "CollectionSchemeManager::checkTimeLine ",
                           "found collectionScheme: " + topCollectionSchemeID +
                               " but time does not match: "
                               "topTime " +
                               std::to_string( topTime ) + " timeFromCollectionScheme " +
                               std::to_string( timeOfInterest ) );
            mTimeLine.pop();
            continue;
        }
        // now we have a dataPair with valid collectionScheme ID, and valid start/stop time
        // Check if it is time to enable/disable this collectionScheme, or else
        // topTime is far down the timeline, it is a timePoint to set up next timer.
        if ( topTime <= currTime )
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
                mLogger.info( "CollectionSchemeManager::checkTimeLine ", enableStr );
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
                mLogger.info( "CollectionSchemeManager::checkTimeLine ", disableStr );
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
        mLogger.trace( "CollectionSchemeManager::checkTimeLine ",
                       "top pair: " + std::to_string( mTimeLine.top().first ) + " " + mTimeLine.top().second +
                           " currTime: " + std::to_string( currTime ) );
    }
    return ret;
}

void
CollectionSchemeManager::addConditionData( const ICollectionSchemePtr &collectionScheme,
                                           ConditionWithCollectedData &conditionData )
{
    conditionData.minimumPublishInterval = collectionScheme->getMinimumPublishIntervalMs();
    conditionData.afterDuration = collectionScheme->getAfterDurationMs();
    conditionData.includeActiveDtcs = collectionScheme->isActiveDTCsIncluded();
    conditionData.triggerOnlyOnRisingEdge = collectionScheme->isTriggerOnlyOnRisingEdge();
    conditionData.probabilityToSend = collectionScheme->getProbabilityToSend();

    /*
     * use for loop to copy signalInfo and CANframe over to avoid error or memory issue
     * This is probably not the fastest way to get things done, but the safest way
     * since the object is not big, so not really slow
     */
    const std::vector<SignalCollectionInfo> &collectionSignals = collectionScheme->getCollectSignals();
    for ( uint32_t i = 0; i < collectionSignals.size(); i++ )
    {
        InspectionMatrixSignalCollectionInfo inspectionSignal = {};
        inspectionSignal.signalID = collectionSignals[i].signalID;
        inspectionSignal.sampleBufferSize = collectionSignals[i].sampleBufferSize;
        inspectionSignal.minimumSampleIntervalMs = collectionSignals[i].minimumSampleIntervalMs;
        inspectionSignal.fixedWindowPeriod = collectionSignals[i].fixedWindowPeriod;
        inspectionSignal.isConditionOnlySignal = collectionSignals[i].isConditionOnlySignal;
        conditionData.signals.emplace_back( inspectionSignal );
    }

    const std::vector<CanFrameCollectionInfo> &collectionCANFrames = collectionScheme->getCollectRawCanFrames();
    for ( uint32_t i = 0; i < collectionCANFrames.size(); i++ )
    {
        InspectionMatrixCanFrameCollectionInfo CANFrame = {};
        CANFrame.frameID = collectionCANFrames[i].frameID;
        CANFrame.channelID = mCANIDTranslator.getChannelNumericID( collectionCANFrames[i].interfaceID );
        CANFrame.sampleBufferSize = collectionCANFrames[i].sampleBufferSize;
        CANFrame.minimumSampleIntervalMs = collectionCANFrames[i].minimumSampleIntervalMs;
        if ( CANFrame.channelID == INVALID_CAN_CHANNEL_NUMERIC_ID )
        {
            mLogger.warn( "CollectionSchemeManager::addConditionData",
                          "Invalid Interface ID provided:" + collectionCANFrames[i].interfaceID );
        }
        else
        {
            conditionData.canFrames.emplace_back( CANFrame );
        }
    }
    // Image capture data
    const std::vector<ImageCollectionInfo> &imageCollectionInfos = collectionScheme->getImageCaptureData();
    for ( const auto &imageInfo : imageCollectionInfos )
    {
        InspectionMatrixImageCollectionInfo imageSettings = {};
        imageSettings.deviceID = imageInfo.deviceID;
        switch ( imageInfo.collectionType )
        {
        case ImageCollectionType::TIME_BASED:
            imageSettings.collectionType = InspectionMatrixImageCollectionType::TIME_BASED;
            imageSettings.beforeDurationMs = imageInfo.beforeDurationMs;
            break;
        case ImageCollectionType::FRAME_BASED:
            imageSettings.collectionType = InspectionMatrixImageCollectionType::FRAME_BASED;
            break;

        default:
            break;
        }
        imageSettings.imageFormat = imageInfo.imageFormat;
        conditionData.imageCollectionInfos.emplace_back( imageSettings );
    }
    conditionData.includeImageCapture = !conditionData.imageCollectionInfos.empty();
    // The rest
    conditionData.metaData.compress = collectionScheme->isCompressionNeeded();
    conditionData.metaData.persist = collectionScheme->isPersistNeeded();
    conditionData.metaData.priority = collectionScheme->getPriority();
    conditionData.metaData.decoderID = collectionScheme->getDecoderManifestID();
    conditionData.metaData.collectionSchemeID = collectionScheme->getCollectionSchemeID();
}

void
CollectionSchemeManager::inspectionMatrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix )
{
    std::stack<const ExpressionNode *> nodeStack;
    std::map<const ExpressionNode *, uint32_t> nodeToIndexMap;
    std::vector<const ExpressionNode *> nodes;
    uint32_t index = 0;
    const ExpressionNode *currNode = nullptr;

    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); it++ )
    {
        ICollectionSchemePtr collectionScheme = it->second;
        ConditionWithCollectedData conditionData;
        addConditionData( collectionScheme, conditionData );

        currNode = collectionScheme->getCondition();
        /* save the old root of this tree */
        conditionData.condition = currNode;
        inspectionMatrix->conditions.emplace_back( conditionData );

        /*
         * The following lines traverse each tree and pack the node addresses into a vector
         * and build a map
         * any order to traverse the tree is OK, here we use in-order.
         */
        while ( currNode != nullptr )
        {
            nodeStack.push( currNode );
            currNode = currNode->left;
        }
        while ( !nodeStack.empty() )
        {
            currNode = nodeStack.top();
            nodeStack.pop();
            nodeToIndexMap[currNode] = index;
            nodes.emplace_back( currNode );
            index++;
            if ( currNode->right != nullptr )
            {
                currNode = currNode->right;
                while ( currNode != nullptr )
                {
                    nodeStack.push( currNode );
                    currNode = currNode->left;
                }
            }
        }
    }

    size_t count = nodes.size();
    /* now we have the count of all nodes from all collectionSchemes, allocate a vector for the output */
    inspectionMatrix->expressionNodeStorage.resize( count );
    /* copy from the old tree node and update left and right children pointers */
    for ( uint32_t i = 0; i < count; i++ )
    {
        inspectionMatrix->expressionNodeStorage[i].nodeType = nodes[i]->nodeType;
        inspectionMatrix->expressionNodeStorage[i].floatingValue = nodes[i]->floatingValue;
        inspectionMatrix->expressionNodeStorage[i].booleanValue = nodes[i]->booleanValue;
        inspectionMatrix->expressionNodeStorage[i].signalID = nodes[i]->signalID;
        inspectionMatrix->expressionNodeStorage[i].function = nodes[i]->function;

        if ( nodes[i]->left != nullptr )
        {
            uint32_t leftIndex = nodeToIndexMap[nodes[i]->left];
            inspectionMatrix->expressionNodeStorage[i].left = &inspectionMatrix->expressionNodeStorage[leftIndex];
        }
        else
        {
            inspectionMatrix->expressionNodeStorage[i].left = nullptr;
        }

        if ( nodes[i]->right != nullptr )
        {
            uint32_t rightIndex = nodeToIndexMap[nodes[i]->right];
            inspectionMatrix->expressionNodeStorage[i].right = &inspectionMatrix->expressionNodeStorage[rightIndex];
        }
        else
        {
            inspectionMatrix->expressionNodeStorage[i].right = nullptr;
        }
    }
    /* update the root of tree with new address */
    for ( uint32_t i = 0; i < inspectionMatrix->conditions.size(); i++ )
    {
        uint32_t newIndex = nodeToIndexMap[inspectionMatrix->conditions[i].condition];
        inspectionMatrix->conditions[i].condition = &inspectionMatrix->expressionNodeStorage[newIndex];
    }
}

void
CollectionSchemeManager::inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
{
    notifyListeners<const std::shared_ptr<const InspectionMatrix> &>(
        &IActiveConditionProcessor::onChangeInspectionMatrix, inspectionMatrix );
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
