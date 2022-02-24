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

#include "CacheAndPersist.h"
#include "ClockHandler.h"
#include "CollectionSchemeIngestion.h"
#include "CollectionSchemeIngestionList.h"
#include "CollectionSchemeManager.h"
#include "DecoderManifestIngestion.h"
#include "Listener.h"
#include "NetworkChannelBinder.h"
#include "OBDOverCANModule.h"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>
#include <queue>
#include <thread>
#include <unistd.h>

// color background definition for printing additionally in gtest
#define ANSI_TXT_GRN "\033[0;32m"
#define ANSI_TXT_MGT "\033[0;35m" // Magenta
#define ANSI_TXT_DFT "\033[0;0m"  // Console default
#define GTEST_BOX "[     cout ] "
#define COUT_GTEST ANSI_TXT_GRN << GTEST_BOX // You could add the Default
// sample print
// std::cout << COUT_GTEST_MGT << "random seed = " << random_seed << ANSI_TXT_DFT << std::endl;
#define COUT_GTEST_MGT COUT_GTEST << ANSI_TXT_MGT

#define UNUSED( x ) (void)( x )
using namespace Aws::IoTFleetWise::DataManagement;
using Aws::IoTFleetWise::Platform::ThreadListeners;

#define SECOND_TO_MILLISECOND( x ) ( 1000 ) * ( x )
class IDecoderManifestTest : public DecoderManifestIngestion
{
public:
    IDecoderManifestTest( std::string id )
        : ID( id )
    {
    }
    IDecoderManifestTest(
        std::string id,
        std::unordered_map<CANInterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> &formatMap,
        std::unordered_map<SignalID, std::pair<CANRawFrameID, CANInterfaceID>> signalToFrameAndNodeID,
        std::unordered_map<SignalID, PIDSignalDecoderFormat> signalIDToPIDDecoderFormat )
        : ID( id )
        , mFormatMap( formatMap )
        , mSignalToFrameAndNodeID( signalToFrameAndNodeID )
        , mSignalIDToPIDDecoderFormat( signalIDToPIDDecoderFormat )
    {
    }

    std::string
    getID() const
    {
        return ID;
    }
    const CANMessageFormat &
    getCANMessageFormat( CANRawFrameID canId, CANInterfaceID interfaceId ) const
    {
        return mFormatMap.at( interfaceId ).at( canId );
    }
    bool
    build()
    {
        return true;
    }
    std::pair<CANRawFrameID, CANInterfaceID>
    getCANFrameAndInterfaceID( SignalID signalId ) const override
    {
        return mSignalToFrameAndNodeID.at( signalId );
    }
    NetworkChannelProtocol
    getNetworkProtocol( SignalID signalID ) const override
    {
        // a simple logic to assign network protocol type to signalID for testing purpose.
        if ( signalID < 0x1000 )
        {
            return RAW_SOCKET;
        }
        else if ( signalID < 0x10000 )
        {
            return OBD;
        }
        else
        {
            return INVALID_PROTOCOL;
        }
    }
    PIDSignalDecoderFormat
    getPIDSignalDecoderFormat( SignalID signalId ) const
    {
        if ( mSignalIDToPIDDecoderFormat.count( signalId ) > 0 )
        {
            return mSignalIDToPIDDecoderFormat.at( signalId );
        }
        return NOT_FOUND_PID_DECODER_FORMAT;
    }

private:
    std::string ID;
    std::unordered_map<CANInterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> mFormatMap;
    std::unordered_map<SignalID, std::pair<CANRawFrameID, CANInterfaceID>> mSignalToFrameAndNodeID;
    std::unordered_map<SignalID, PIDSignalDecoderFormat> mSignalIDToPIDDecoderFormat;
};
class ICollectionSchemeTest : public CollectionSchemeIngestion
{
public:
    ICollectionSchemeTest( std::string collectionSchemeID,
                           std::string DMID,
                           uint64_t start,
                           uint64_t stop,
                           Signals_t signalsIn,
                           RawCanFrames_t rawCanFrmsIn )
        : collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
        , signals( signalsIn )
        , rawCanFrms( rawCanFrmsIn )
        , root( nullptr )
    {
    }
    ICollectionSchemeTest( std::string collectionSchemeID,
                           std::string DMID,
                           uint64_t start,
                           uint64_t stop,
                           Signals_t signalsIn,
                           RawCanFrames_t rawCanFrmsIn,
                           ImagesDataType imagesDataIn )
        : collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
        , signals( signalsIn )
        , rawCanFrms( rawCanFrmsIn )
        , imagesData( imagesDataIn )
        , root( nullptr )
    {
    }
    ICollectionSchemeTest( std::string collectionSchemeID, std::string DMID, uint64_t start, uint64_t stop )
        : collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
        , root( nullptr )
    {
    }
    ICollectionSchemeTest(
        std::string collectionSchemeID, std::string DMID, uint64_t start, uint64_t stop, ExpressionNode *root )
        : collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
        , root( root )
    {
    }
    const std::string &
    getCollectionSchemeID() const
    {
        return collectionSchemeID;
    }
    const std::string &
    getDecoderManifestID() const
    {
        return decoderManifestID;
    }
    uint64_t
    getStartTime() const
    {
        return startTime;
    }
    uint64_t
    getExpiryTime() const
    {
        return expiryTime;
    }
    const Signals_t &
    getCollectSignals() const
    {
        return signals;
    }
    const RawCanFrames_t &
    getCollectRawCanFrames() const
    {
        return rawCanFrms;
    }
    const ImagesDataType &
    getImageCaptureData() const
    {
        return imagesData;
    }
    const struct ExpressionNode *
    getCondition() const
    {
        return root;
    }
    bool
    build() override
    {
        return true;
    }

private:
    std::string collectionSchemeID;
    std::string decoderManifestID;
    uint64_t startTime;
    uint64_t expiryTime;
    Signals_t signals;
    RawCanFrames_t rawCanFrms;
    ImagesDataType imagesData;
    ExpressionNode *root;
};

class ICollectionSchemeListTest : public CollectionSchemeIngestionList
{
public:
    ICollectionSchemeListTest( std::vector<ICollectionSchemePtr> &list )
        : mCollectionSchemeTest( list )
    {
    }
    const std::vector<ICollectionSchemePtr> &
    getCollectionSchemes() const override
    {
        return mCollectionSchemeTest;
    }
    bool
    build() override
    {
        return true;
    }

public:
    std::vector<ICollectionSchemePtr> mCollectionSchemeTest;
};

/* mock producer class that sends update to PM mocking PI */
class CollectionSchemeManagerTestProducer
    : public Aws::IoTFleetWise::Platform::ThreadListeners<CollectionSchemeManagementListener>
{
public:
    CollectionSchemeManagerTestProducer()
    {
    }
    ~CollectionSchemeManagerTestProducer()
    {
    }
};

/* mock Network Channel Binder class that receive decoder dictionary update from PM */
class NetworkChannelBinderMock : public NetworkChannelBinder
{
public:
    NetworkChannelBinderMock()
        : mUpdateFlag( false )
    {
    }

    ~NetworkChannelBinderMock()
    {
    }

    void
    onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                NetworkChannelProtocol networkProtocol ) override
    {
        NetworkChannelBinder::onChangeOfActiveDictionary( dictionary, networkProtocol );
        mUpdateFlag = true;
    }

    void
    setUpdateFlag( bool flag )
    {
        mUpdateFlag = flag;
    }

    bool
    getUpdateFlag()
    {
        return mUpdateFlag;
    }

private:
    // This flag is used for testing whether Network Channel Binder received the update
    bool mUpdateFlag;
};

/* mock OBDOverCANModule class that receive decoder dictionary update from PM */
class OBDOverCANModuleMock : public OBDOverCANModule
{
public:
    OBDOverCANModuleMock()
        : mUpdateFlag( false )
    {
    }
    ~OBDOverCANModuleMock()
    {
    }
    void
    onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                NetworkChannelProtocol networkProtocol ) override
    {
        OBDOverCANModule::onChangeOfActiveDictionary( dictionary, networkProtocol );
        mUpdateFlag = true;
    }
    void
    setUpdateFlag( bool flag )
    {
        mUpdateFlag = flag;
    }
    bool
    getUpdateFlag()
    {
        return mUpdateFlag;
    }

private:
    // This flag is used for testing whether Network Channel Consumer received the update
    bool mUpdateFlag;
};

/* mock Collection Inspection Engine class that receive Inspection Matrix update from PM */
class CollectionInspectionEngineMock : public CollectionInspectionEngine
{
public:
    CollectionInspectionEngineMock()
        : mUpdateFlag( false )
    {
    }
    ~CollectionInspectionEngineMock()
    {
    }
    void
    onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix ) override
    {
        CollectionInspectionEngine::onChangeInspectionMatrix( inspectionMatrix );
        mUpdateFlag = true;
    }
    void
    setUpdateFlag( bool flag )
    {
        mUpdateFlag = flag;
    }
    bool
    getUpdateFlag()
    {
        return mUpdateFlag;
    }

private:
    // This flag is used for testing whether the listener received the update
    bool mUpdateFlag;
};

class CollectionSchemeManagerTest : public CollectionSchemeManager
{
public:
    CollectionSchemeManagerTest()
    {
    }
    CollectionSchemeManagerTest( std::string dm_id )
        : CollectionSchemeManager( dm_id )
    {
    }

    void
    myRegisterListener()
    {
        mProducer.subscribeListener( this );
    }
    void
    myInvokeCollectionScheme()
    {
        mProducer.notifyListeners<const ICollectionSchemeListPtr &>(
            &CollectionSchemeManagementListener::onCollectionSchemeUpdate, mPlTest );
    }
    void
    myInvokeDecoderManifest()
    {
        mProducer.notifyListeners<const IDecoderManifestPtr &>(
            &CollectionSchemeManagementListener::onDecoderManifestUpdate, mDmTest );
    }
    void
    updateAvailable()
    {
        CollectionSchemeManager::updateAvailable();
    }
    bool
    rebuildMapsandTimeLine( const TimePointInMsec &currTime )
    {
        return ( CollectionSchemeManager::rebuildMapsandTimeLine( currTime ) );
    }
    bool
    updateMapsandTimeLine( const TimePointInMsec &currTime )
    {
        return CollectionSchemeManager::updateMapsandTimeLine( currTime );
    }
    bool
    checkTimeLine( const TimePointInMsec &currTime )
    {
        return ( CollectionSchemeManager::checkTimeLine( currTime ) );
    }
    void
    decoderDictionaryExtractor(
        std::map<NetworkChannelProtocol, std::shared_ptr<CANDecoderDictionary>> &decoderDictionaryMap )
    {
        return CollectionSchemeManager::decoderDictionaryExtractor( decoderDictionaryMap );
    }
    void
    decoderDictionaryUpdater(
        std::map<NetworkChannelProtocol, std::shared_ptr<CANDecoderDictionary>> &decoderDictionaryMap )
    {
        return CollectionSchemeManager::decoderDictionaryUpdater( decoderDictionaryMap );
    }
    const std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> &
    getTimeLine()
    {
        return CollectionSchemeManager::mTimeLine;
    }
    void
    setDecoderManifest( IDecoderManifestPtr dm )
    {
        CollectionSchemeManager::mDecoderManifest = dm;
    }
    void
    setCollectionSchemeList( ICollectionSchemeListPtr pl )
    {
        CollectionSchemeManager::mCollectionSchemeList = pl;
    }
    void
    setCollectionSchemePersistency( std::shared_ptr<ICacheAndPersist> pp )
    {
        CollectionSchemeManager::mSchemaPersistency = pp;
    }
    void
    inspectionMatrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix )
    {
        CollectionSchemeManager::inspectionMatrixExtractor( inspectionMatrix );
    }
    void
    inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
    {
        CollectionSchemeManager::inspectionMatrixUpdater( inspectionMatrix );
    }
    bool
    retrieve( DataType retrieveType )
    {
        return CollectionSchemeManager::retrieve( retrieveType );
    }

    void
    store( DataType storeType )
    {
        CollectionSchemeManager::store( storeType );
    }

    void
    setmCollectionSchemeAvailable( bool val )
    {
        mCollectionSchemeAvailable = val;
    }
    bool
    getmCollectionSchemeAvailable()
    {
        return mCollectionSchemeAvailable;
    }

    void
    setmDecoderManifestAvailable( bool val )
    {
        mDecoderManifestAvailable = val;
    }

    bool
    getmDecoderManifestAvailable()
    {
        return mDecoderManifestAvailable;
    }

    void
    setmProcessCollectionScheme( bool val )
    {
        mProcessCollectionScheme = val;
    }

    bool
    getmProcessCollectionScheme()
    {
        return mProcessCollectionScheme;
    }

    void
    setmProcessDecoderManifest( bool val )
    {
        mProcessDecoderManifest = val;
    }
    bool
    getmProcessDecoderManifest()
    {
        return mProcessDecoderManifest;
    }

public:
    CollectionSchemeManagerTestProducer mProducer;
    IDecoderManifestPtr mDmTest;
    std::shared_ptr<ICollectionSchemeListTest> mPlTest;

protected:
};
