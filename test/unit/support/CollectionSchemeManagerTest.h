// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CollectionInspectionAPITypes.h"
#include "CollectionInspectionEngine.h"
#include "CollectionInspectionWorkerThread.h"
#include "CollectionSchemeIngestion.h"
#include "CollectionSchemeIngestionList.h"
#include "DecoderManifestIngestion.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "MessageTypes.h"
#include "OBDOverCANModule.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include <algorithm> // IWYU pragma: keep
#include <cstdint>
#include <memory>
#include <sstream> // IWYU pragma: keep
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// color background definition for printing additionally in gtest
#define ANSI_TXT_GRN "\033[0;32m"
#define ANSI_TXT_MGT "\033[0;35m" // Magenta
#define ANSI_TXT_DFT "\033[0;0m"  // Console default
#define GTEST_BOX "[     cout ] "
#define COUT_GTEST ANSI_TXT_GRN << GTEST_BOX // You could add the Default
// sample print
// std::cout << COUT_GTEST_MGT << "random seed = " << random_seed << ANSI_TXT_DFT << std::endl;
#define COUT_GTEST_MGT COUT_GTEST << ANSI_TXT_MGT
#define SECOND_TO_MILLISECOND( x ) ( 1000 ) * ( x )

namespace Aws
{
namespace IoTFleetWise
{

class IDecoderManifestTest : public DecoderManifestIngestion
{
public:
    IDecoderManifestTest( SyncID id )
        : ID( id )
    {
    }
    IDecoderManifestTest(
        SyncID id,
        std::unordered_map<InterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> formatMap,
        std::unordered_map<SignalID, std::pair<CANRawFrameID, InterfaceID>> signalToFrameAndNodeID,
        std::unordered_map<SignalID, PIDSignalDecoderFormat> signalIDToPIDDecoderFormat
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        ,
        std::unordered_map<SignalID, ComplexSignalDecoderFormat> complexSignalMap =
            std::unordered_map<SignalID, ComplexSignalDecoderFormat>(),
        std::unordered_map<ComplexDataTypeId, ComplexDataElement> complexDataTypeMap =
            std::unordered_map<ComplexDataTypeId, ComplexDataElement>()
#endif
            )
        : ID( id )
        , mFormatMap( std::move( formatMap ) )
        , mSignalToFrameAndNodeID( std::move( signalToFrameAndNodeID ) )
        , mSignalIDToPIDDecoderFormat( std::move( signalIDToPIDDecoderFormat ) )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        , mComplexSignalMap( std::move( complexSignalMap ) )
        , mComplexDataTypeMap( std::move( complexDataTypeMap ) )
#endif

    {
    }

    SyncID
    getID() const
    {
        return ID;
    }
    const CANMessageFormat &
    getCANMessageFormat( CANRawFrameID canId, InterfaceID interfaceId ) const
    {
        return mFormatMap.at( interfaceId ).at( canId );
    }
    bool
    build()
    {
        return true;
    }
    std::pair<CANRawFrameID, InterfaceID>
    getCANFrameAndInterfaceID( SignalID signalId ) const override
    {
        return mSignalToFrameAndNodeID.at( signalId );
    }
    VehicleDataSourceProtocol
    getNetworkProtocol( SignalID signalID ) const override
    {
        // a simple logic to assign network protocol type to signalID for testing purpose.
        if ( signalID == INVALID_SIGNAL_ID )
        {
            return VehicleDataSourceProtocol::INVALID_PROTOCOL;
        }
        else if ( signalID < 0x1000 )
        {
            return VehicleDataSourceProtocol::RAW_SOCKET;
        }
        else if ( signalID < 0x2000 )
        {
            return VehicleDataSourceProtocol::OBD;
        }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        else if ( signalID < 0xFFFFFF00 )
        {
            return VehicleDataSourceProtocol::COMPLEX_DATA;
        }
#endif
        else
        {
            return VehicleDataSourceProtocol::INVALID_PROTOCOL;
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

    SignalType
    getSignalType( SignalID signalID ) const override
    {
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        if ( getNetworkProtocol( signalID ) == VehicleDataSourceProtocol::COMPLEX_DATA )
        {
            return SignalType::UNKNOWN;
        }
#else
        static_cast<void>( signalID );
#endif
        return SignalType::DOUBLE;
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ComplexSignalDecoderFormat
    getComplexSignalDecoderFormat( SignalID signalID ) const override
    {
        auto s = mComplexSignalMap.find( signalID );
        if ( s == mComplexSignalMap.end() )
        {
            return ComplexSignalDecoderFormat();
        }
        return s->second;
    }

    ComplexDataElement
    getComplexDataType( ComplexDataTypeId typeId ) const override
    {
        auto c = mComplexDataTypeMap.find( typeId );
        if ( c == mComplexDataTypeMap.end() )
        {
            return ComplexDataElement( InvalidComplexVariant() );
        }
        return c->second;
    }
#endif

private:
    SyncID ID;
    std::unordered_map<InterfaceID, std::unordered_map<CANRawFrameID, CANMessageFormat>> mFormatMap;
    std::unordered_map<SignalID, std::pair<CANRawFrameID, InterfaceID>> mSignalToFrameAndNodeID;
    std::unordered_map<SignalID, PIDSignalDecoderFormat> mSignalIDToPIDDecoderFormat;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::unordered_map<SignalID, ComplexSignalDecoderFormat> mComplexSignalMap;
    std::unordered_map<ComplexDataTypeId, ComplexDataElement> mComplexDataTypeMap;
#endif
};
class ICollectionSchemeTest : public CollectionSchemeIngestion
{
public:
    ICollectionSchemeTest( SyncID collectionSchemeID,
                           SyncID DMID,
                           uint64_t start,
                           uint64_t stop,
                           const Signals_t &signalsIn,
                           const RawCanFrames_t &rawCanFrmsIn
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                           ,
                           const PartialSignalIDLookup &partialSignalLookup = PartialSignalIDLookup()
#endif
                               )
        : CollectionSchemeIngestion(
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
              std::make_shared<CollectionSchemeIngestion::PartialSignalIDLookup>()
#endif
                  )
        , collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
        , signals( signalsIn )
        , rawCanFrms( rawCanFrmsIn )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        , partialSignalLookup( partialSignalLookup )
#endif
        , root( nullptr )
    {
    }
    ICollectionSchemeTest( SyncID collectionSchemeID,
                           SyncID DMID,
                           uint64_t start,
                           uint64_t stop
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                           ,
                           S3UploadMetadata s3UploadMetadata = S3UploadMetadata()
#endif
                               )
        : CollectionSchemeIngestion(
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
              std::make_shared<CollectionSchemeIngestion::PartialSignalIDLookup>()
#endif
                  )
        , collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        , s3UploadMetadata( s3UploadMetadata )
#endif
        , root( nullptr )
    {
    }
    ICollectionSchemeTest( SyncID collectionSchemeID, SyncID DMID, uint64_t start, uint64_t stop, ExpressionNode *root )
        : CollectionSchemeIngestion(
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
              std::make_shared<CollectionSchemeIngestion::PartialSignalIDLookup>()
#endif
                  )
        , collectionSchemeID( collectionSchemeID )
        , decoderManifestID( DMID )
        , startTime( start )
        , expiryTime( stop )
        , root( root )
    {
    }
    const SyncID &
    getCollectionSchemeID() const
    {
        return collectionSchemeID;
    }
    const SyncID &
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
    const ExpressionNode *
    getCondition() const
    {
        return root;
    }
    bool
    build() override
    {
        return true;
    }

    bool
    isReady() const override
    {
        return true;
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    const PartialSignalIDLookup &
    getPartialSignalIdToSignalPathLookupTable() const override
    {
        return partialSignalLookup;
    }
    S3UploadMetadata
    getS3UploadMetadata() const override
    {
        return s3UploadMetadata;
    }
#endif

private:
    SyncID collectionSchemeID;
    SyncID decoderManifestID;
    uint64_t startTime;
    uint64_t expiryTime;
    Signals_t signals;
    RawCanFrames_t rawCanFrms;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    PartialSignalIDLookup partialSignalLookup;
    S3UploadMetadata s3UploadMetadata;
#endif
    ExpressionNode *root;
};

class ICollectionSchemeListTest : public CollectionSchemeIngestionList
{
public:
    ICollectionSchemeListTest( const std::vector<ICollectionSchemePtr> &list )
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

    bool
    isReady() const override
    {
        return true;
    }

public:
    std::vector<ICollectionSchemePtr> mCollectionSchemeTest;
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
    onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary, VehicleDataSourceProtocol networkProtocol )
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
    // This flag is used for testing whether Vehicle Data Consumer received the update
    bool mUpdateFlag;
};

/* mock Collection Inspection Engine class that receive Inspection Matrix update from PM */
class CollectionInspectionWorkerThreadMock : public CollectionInspectionWorkerThread
{
public:
    CollectionInspectionWorkerThreadMock()
        : CollectionInspectionWorkerThread( mEngine )
        , mInspectionMatrixUpdateFlag( false )
    {
    }
    ~CollectionInspectionWorkerThreadMock()
    {
    }
    void
    onChangeInspectionMatrix( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
    {
        CollectionInspectionWorkerThread::onChangeInspectionMatrix( inspectionMatrix );
        mInspectionMatrixUpdateFlag = true;
    }
    void
    setInspectionMatrixUpdateFlag( bool flag )
    {
        mInspectionMatrixUpdateFlag = flag;
    }
    bool
    getInspectionMatrixUpdateFlag()
    {
        return mInspectionMatrixUpdateFlag;
    }

private:
    CollectionInspectionEngine mEngine;
    // This flag is used for testing whether the listener received the Inspection Matrix update
    bool mInspectionMatrixUpdateFlag;
};

} // namespace IoTFleetWise
} // namespace Aws
