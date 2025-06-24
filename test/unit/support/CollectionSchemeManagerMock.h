// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CollectionSchemeManagerTest.h"
#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionSchemeIngestion.h"
#include "aws/iotfleetwise/CollectionSchemeIngestionList.h"
#include "aws/iotfleetwise/CollectionSchemeManager.h"
#include "aws/iotfleetwise/DecoderManifestIngestion.h"
#include "aws/iotfleetwise/Listener.h"
#include <algorithm>
#include <atomic>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include "aws/iotfleetwise/LastKnownStateIngestion.h"
#endif

#define SECOND_TO_MILLISECOND( x ) ( 1000 ) * ( x )

namespace Aws
{
namespace IoTFleetWise
{

using uint8Ptr = std::uint8_t *;
using vectorUint8 = std::vector<uint8_t>;
using vectorICollectionSchemePtr = std::vector<std::shared_ptr<ICollectionScheme>>;

class CollectionSchemeManagerWrapper : public CollectionSchemeManager
{
public:
    CollectionSchemeManagerWrapper( std::shared_ptr<CacheAndPersist> schemaPersistencyPtr,
                                    CANInterfaceIDTranslator &canIDTranslator,
                                    std::shared_ptr<CheckinSender> checkinSender,
                                    RawData::BufferManager *rawDataBufferManager = nullptr )

        : CollectionSchemeManager( schemaPersistencyPtr, canIDTranslator, checkinSender, rawDataBufferManager )
    {
    }

    CollectionSchemeManagerWrapper( std::shared_ptr<CacheAndPersist> schemaPersistencyPtr,
                                    CANInterfaceIDTranslator &canIDTranslator,
                                    std::shared_ptr<CheckinSender> checkinSender,
                                    SyncID decoderManifestID,
                                    RawData::BufferManager *rawDataBufferManager = nullptr
#ifdef FWE_FEATURE_REMOTE_COMMANDS
                                    ,
                                    GetActuatorNamesCallback getActuatorNamesCallback = nullptr
#endif

                                    )
        : CollectionSchemeManager( schemaPersistencyPtr,
                                   canIDTranslator,
                                   checkinSender,
                                   rawDataBufferManager
#ifdef FWE_FEATURE_REMOTE_COMMANDS
                                   ,
                                   getActuatorNamesCallback
#endif
          )
    {
        mCurrentDecoderManifestID = decoderManifestID;
    }
    CollectionSchemeManagerWrapper( std::shared_ptr<CacheAndPersist> schemaPersistencyPtr,
                                    CANInterfaceIDTranslator &canIDTranslator,
                                    std::shared_ptr<CheckinSender> checkinSender,
                                    SyncID decoderManifestID,
                                    std::map<SyncID, std::shared_ptr<ICollectionScheme>> &mapEnabled,
                                    std::map<SyncID, std::shared_ptr<ICollectionScheme>> &mapIdle,
                                    RawData::BufferManager *rawDataBufferManager = nullptr )
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
    matrixExtractor( InspectionMatrix &inspectionMatrix, FetchMatrix &fetchMatrix )
    {
        CollectionSchemeManager::matrixExtractor( inspectionMatrix, fetchMatrix );
    }

    void
    inspectionMatrixUpdater( std::shared_ptr<const InspectionMatrix> inspectionMatrix )
    {
        CollectionSchemeManager::inspectionMatrixUpdater( inspectionMatrix );
    }

    void
    fetchMatrixUpdater( std::shared_ptr<const FetchMatrix> fetchMatrix )
    {
        CollectionSchemeManager::fetchMatrixUpdater( fetchMatrix );
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    void
    updateRawDataBufferConfigComplexSignals(
        Aws::IoTFleetWise::ComplexDataDecoderDictionary *complexDataDecoderDictionary,
        std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
    {
        CollectionSchemeManager::updateRawDataBufferConfigComplexSignals( complexDataDecoderDictionary,
                                                                          updatedSignals );
    }
#endif

    void
    updateRawDataBufferConfigStringSignals(
        std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
    {
        CollectionSchemeManager::updateRawDataBufferConfigStringSignals( updatedSignals );
    }

    void
    setCollectionSchemePersistency( std::shared_ptr<CacheAndPersist> collectionSchemePersistency )
    {
        CollectionSchemeManager::mSchemaPersistency = collectionSchemePersistency;
    }
    void
    setDecoderManifest( std::shared_ptr<IDecoderManifest> dm )
    {
        mDecoderManifest = dm;
    }
    void
    setCollectionSchemeList( std::shared_ptr<ICollectionSchemeList> pl )
    {
        mCollectionSchemeList = pl;
    }
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    void
    myInvokeStateTemplates()
    {
        this->onStateTemplatesChanged( mLastKnownStateIngestionTest );
    }

    bool
    getmProcessStateTemplates()
    {
        return mProcessStateTemplates;
    }

    void
    setLastKnownStateIngestion( std::shared_ptr<LastKnownStateIngestion> lastKnownStateIngestion )
    {
        mLastKnownStateIngestion = lastKnownStateIngestion;
    }

    void
    setStateTemplates( std::shared_ptr<const StateTemplatesDiff> stateTemplates )
    {
        for ( auto &stateTemplate : stateTemplates->stateTemplatesToAdd )
        {
            mStateTemplates.emplace( stateTemplate->id, stateTemplate );
        }
    }
#endif

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
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap )
    {

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        InspectionMatrix inspectionMatrix;
#endif
        return CollectionSchemeManager::decoderDictionaryExtractor( decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                                                    ,
                                                                    inspectionMatrix
#endif
        );
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    void
    decoderDictionaryExtractor(
        std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap,
        InspectionMatrix &inspectionMatrix )
    {
        return CollectionSchemeManager::decoderDictionaryExtractor( decoderDictionaryMap, inspectionMatrix );
    }
#endif

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

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    void
    generateInspectionMatrix( std::shared_ptr<InspectionMatrix> &inspectionMatrix,
                              std::shared_ptr<FetchMatrix> &fetchMatrix )
    {
        inspectionMatrix = std::make_shared<InspectionMatrix>();
        fetchMatrix = std::make_shared<FetchMatrix>();
        this->matrixExtractor( *inspectionMatrix, *fetchMatrix );
        this->inspectionMatrixUpdater( inspectionMatrix );
    }
#endif

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
    std::shared_ptr<IDecoderManifest> mDmTest;
    std::shared_ptr<ICollectionSchemeListTest> mPlTest;
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    std::shared_ptr<LastKnownStateIngestion> mLastKnownStateIngestionTest;
#endif
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

    // const std::vector<std::shared_ptr<ICollectionScheme>> &getCollectionSchemes() const override;
    MOCK_METHOD( const std::vector<std::shared_ptr<ICollectionScheme>> &,
                 getCollectionSchemes,
                 (),
                 ( const, override ) );

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

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
class LastKnownStateIngestionMock : public LastKnownStateIngestion
{
public:
    MOCK_METHOD( bool, build, () );
    MOCK_METHOD( std::shared_ptr<const StateTemplatesDiff>, getStateTemplatesDiff, (), ( const ) );
    MOCK_METHOD( const std::vector<uint8_t> &, getData, (), ( const ) );
};
#endif

} // namespace IoTFleetWise
} // namespace Aws
