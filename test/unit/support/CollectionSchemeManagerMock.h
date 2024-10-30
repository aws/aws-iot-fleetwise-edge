// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CacheAndPersist.h"
#include "ClockHandler.h"
#include "CollectionSchemeIngestion.h"
#include "CollectionSchemeIngestionList.h"
#include "CollectionSchemeManager.h"
#include "CollectionSchemeManagerTest.h"
#include "DecoderManifestIngestion.h"
#include "Listener.h"
#include <algorithm>
#include <atomic>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#define SECOND_TO_MILLISECOND( x ) ( 1000 ) * ( x )

namespace Aws
{
namespace IoTFleetWise
{

using uint8Ptr = std::uint8_t *;
using vectorUint8 = std::vector<uint8_t>;
using vectorICollectionSchemePtr = std::vector<ICollectionSchemePtr>;

class CollectionSchemeManagerWrapper : public CollectionSchemeManager
{
public:
    CollectionSchemeManagerWrapper( std::shared_ptr<CacheAndPersist> schemaPersistencyPtr,
                                    CANInterfaceIDTranslator &canIDTranslator,
                                    std::shared_ptr<CheckinSender> checkinSender,
                                    std::shared_ptr<RawData::BufferManager> rawDataBufferManager = nullptr )

        : CollectionSchemeManager( schemaPersistencyPtr, canIDTranslator, checkinSender, rawDataBufferManager )
    {
    }

    CollectionSchemeManagerWrapper( std::shared_ptr<CacheAndPersist> schemaPersistencyPtr,
                                    CANInterfaceIDTranslator &canIDTranslator,
                                    std::shared_ptr<CheckinSender> checkinSender,
                                    SyncID decoderManifestID,
                                    std::shared_ptr<RawData::BufferManager> rawDataBufferManager = nullptr )
        : CollectionSchemeManager( schemaPersistencyPtr, canIDTranslator, checkinSender, rawDataBufferManager )
    {
        mCurrentDecoderManifestID = decoderManifestID;
    }
    CollectionSchemeManagerWrapper( std::shared_ptr<CacheAndPersist> schemaPersistencyPtr,
                                    CANInterfaceIDTranslator &canIDTranslator,
                                    std::shared_ptr<CheckinSender> checkinSender,
                                    SyncID decoderManifestID,
                                    std::map<SyncID, ICollectionSchemePtr> &mapEnabled,
                                    std::map<SyncID, ICollectionSchemePtr> &mapIdle,
                                    std::shared_ptr<RawData::BufferManager> rawDataBufferManager = nullptr )
        : CollectionSchemeManager( schemaPersistencyPtr, canIDTranslator, checkinSender, rawDataBufferManager )
    {
        mCurrentDecoderManifestID = decoderManifestID;
        mEnabledCollectionSchemeMap = mapEnabled;
        mIdleCollectionSchemeMap = mapIdle;
    }

    void
    myInvokeCollectionScheme()
    {
        this->onCollectionSchemeUpdate( mPlTest );
    }
    void
    myInvokeDecoderManifest()
    {
        this->onDecoderManifestUpdate( mDmTest );
    }
    void
    updateAvailable()
    {
        CollectionSchemeManager::updateAvailable();
    }

    void
    matrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix )
    {
        CollectionSchemeManager::matrixExtractor( inspectionMatrix );
    }

    void
    inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
    {
        CollectionSchemeManager::inspectionMatrixUpdater( inspectionMatrix );
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    void
    updateRawDataBufferConfigComplexSignals(
        std::shared_ptr<Aws::IoTFleetWise::ComplexDataDecoderDictionary> complexDataDecoderDictionary,
        std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
    {
        CollectionSchemeManager::updateRawDataBufferConfigComplexSignals( complexDataDecoderDictionary,
                                                                          updatedSignals );
    }
#endif

    void
    setCollectionSchemePersistency( const std::shared_ptr<CacheAndPersist> &collectionSchemePersistency )
    {
        CollectionSchemeManager::mSchemaPersistency = collectionSchemePersistency;
    }
    void
    setDecoderManifest( const IDecoderManifestPtr &dm )
    {
        // CollectionSchemeManager::mDecoderManifest = dm;
        mDecoderManifest = dm;
    }
    void
    setCollectionSchemeList( const ICollectionSchemeListPtr &pl )
    {
        mCollectionSchemeList = pl;
    }

    void
    setTimeLine( const std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>> &TimeLine )
    {
        mTimeLine = TimeLine;
    }

    std::priority_queue<TimeData, std::vector<TimeData>, std::greater<TimeData>>
    getTimeLine()
    {
        return mTimeLine;
    }

    void
    decoderDictionaryExtractor(
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        ,
        std::shared_ptr<InspectionMatrix> inspectionMatrix = nullptr
#endif
    )
    {
        return CollectionSchemeManager::decoderDictionaryExtractor( decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                                                    ,
                                                                    inspectionMatrix
#endif
        );
    }

    void
    decoderDictionaryUpdater(
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap )
    {
        return CollectionSchemeManager::decoderDictionaryUpdater( decoderDictionaryMap );
    }

    bool
    rebuildMapsandTimeLine( const TimePoint &currTime )
    {
        return CollectionSchemeManager::rebuildMapsandTimeLine( currTime );
    }

    bool
    updateMapsandTimeLine( const TimePoint &currTime )
    {
        return CollectionSchemeManager::updateMapsandTimeLine( currTime );
    }

    bool
    checkTimeLine( const TimePoint &currTime )
    {
        return CollectionSchemeManager::checkTimeLine( currTime );
    }

    void
    updateCheckinDocuments()
    {
        CollectionSchemeManager::updateCheckinDocuments();
    }

    void
    store( DataType storeType )
    {
        CollectionSchemeManager::store( storeType );
    }

    bool
    retrieve( DataType retrieveType )
    {
        return CollectionSchemeManager::retrieve( retrieveType );
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
    IDecoderManifestPtr mDmTest;
    std::shared_ptr<ICollectionSchemeListTest> mPlTest;
};

class mockCollectionScheme : public CollectionSchemeIngestion
{
public:
    mockCollectionScheme()
        : CollectionSchemeIngestion(
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
              std::make_shared<CollectionSchemeIngestion::PartialSignalIDLookup>()
#endif
          )
    {
    }

    // bool build() override;
    MOCK_METHOD( bool, build, (), ( override ) );
    // const SyncID &getCollectionSchemeID()
    MOCK_METHOD( const SyncID &, getCollectionSchemeID, (), ( const, override ) );

    // virtual const SyncID &getDecoderManifestID() const = 0;
    MOCK_METHOD( const SyncID &, getDecoderManifestID, (), ( const, override ) );
    // virtual uint64_t getStartTime() const = 0;
    MOCK_METHOD( uint64_t, getStartTime, (), ( const, override ) );
    // virtual uint64_t getExpiryTime() const = 0;
    MOCK_METHOD( uint64_t, getExpiryTime, (), ( const, override ) );
};

class mockDecoderManifest : public DecoderManifestIngestion
{
public:
    // virtual SyncID getID() const = 0;
    MOCK_METHOD( SyncID, getID, (), ( const, override ) );

    // bool build() override;
    MOCK_METHOD( bool, build, (), ( override ) );

    // bool copyData( const std::uint8_t *inputBuffer, const size_t size ) override;
    MOCK_METHOD( bool, copyData, ( const std::uint8_t *, const size_t ), ( override ) );

    // inline const std::vector<uint8_t>getData() const override
    MOCK_METHOD( const std::vector<uint8_t> &, getData, (), ( const, override ) );
};

class mockCollectionSchemeList : public CollectionSchemeIngestionList
{
public:
    // bool build() override;
    MOCK_METHOD( bool, build, (), ( override ) );

    // const std::vector<ICollectionSchemePtr> &getCollectionSchemes() const override;
    MOCK_METHOD( const std::vector<ICollectionSchemePtr> &, getCollectionSchemes, (), ( const, override ) );

    MOCK_METHOD( bool, copyData, ( const std::uint8_t *, const size_t ), ( override ) );
    MOCK_METHOD( const std::vector<uint8_t> &, getData, (), ( const, override ) );
};

class mockCacheAndPersist : public CacheAndPersist
{
public:
    // ErrorCode write( const uint8_t *bufPtr, size_t size, DataType dataType, const std::string &filename );
    MOCK_METHOD( ErrorCode, write, (const uint8_t *, size_t, DataType, const std::string &), ( override ) );

    // size_t getSize( DataType dataType, const std::string &filename );
    MOCK_METHOD( size_t, getSize, (DataType, const std::string &), ( override ) );

    // ErrorCode read( uint8_t *const readBufPtr, size_t size, DataType dataType, const std::string &filename );
    MOCK_METHOD( ErrorCode, read, (uint8_t *const, size_t, DataType, const std::string &), ( override ) );
};

} // namespace IoTFleetWise
} // namespace Aws
