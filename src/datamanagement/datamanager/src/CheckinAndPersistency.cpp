// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CollectionSchemeIngestionList.h"
#include "CollectionSchemeManager.h"
#include "DecoderManifestIngestion.h"
#include "EnumUtility.h"
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Utility;
void
CollectionSchemeManager::prepareCheckinTimer()
{
    TimePointInMsec currTime = mClock->timeSinceEpochMs();
    TimeData checkinData = std::make_pair( currTime, CHECKIN );
    mTimeLine.push( checkinData );
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

bool
CollectionSchemeManager::retrieve( DataType retrieveType )
{
    size_t protoSize = 0;
    ErrorCode ret = ErrorCode::SUCCESS;
    std::vector<uint8_t> protoOutput;
    std::string infoStr;
    std::string errStr;

    if ( mSchemaPersistency == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::retrieve",
                       "Failed to acquire a valid handle on the scheme local persistency module " );
        return false;
    }
    switch ( retrieveType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        infoStr = "Retrieved a CollectionSchemeList of size ";
        errStr = "Failed to retrieve the CollectionSchemeList from the persistency module due to an error :";
        break;
    case DataType::DECODER_MANIFEST:
        infoStr = "Retrieved a DecoderManifest of size ";
        errStr = "Failed to retrieve the DecoderManifest from the persistency module due to an error :";
        break;
    default:
        mLogger.error( "CollectionSchemeManager::retrieve",
                       " unknown error : " + std::to_string( toUType( retrieveType ) ) );
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
    if ( ret != ErrorCode::SUCCESS )
    {
        mLogger.error( "CollectionSchemeManager::retrieve", errStr + mSchemaPersistency->getErrorString( ret ) );
        return false;
    }
    mLogger.info( "CollectionSchemeManager::retrieve", infoStr + std::to_string( protoSize ) + " successfully." );
    if ( retrieveType == DataType::COLLECTION_SCHEME_LIST )
    {
        // updating mCollectionSchemeList
        if ( mCollectionSchemeList == nullptr )
        {
            mCollectionSchemeList = std::make_shared<CollectionSchemeIngestionList>();
        }
        mCollectionSchemeList->copyData( protoOutput.data(), protoSize );
        mProcessCollectionScheme = true;
    }
    else if ( retrieveType == DataType::DECODER_MANIFEST )
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
    ErrorCode ret = ErrorCode::SUCCESS;
    std::vector<uint8_t> protoInput;
    std::string logStr;

    if ( mSchemaPersistency == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::store",
                       "Failed to acquire a valid handle on the scheme local persistency module" );
        return;
    }
    if ( storeType == DataType::COLLECTION_SCHEME_LIST && mCollectionSchemeList == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::store", "Invalid CollectionSchemeList" );
        return;
    }
    if ( storeType == DataType::DECODER_MANIFEST && mDecoderManifest == nullptr )
    {
        mLogger.error( "CollectionSchemeManager::store", "Invalid DecoderManifest" );
        return;
    }
    switch ( storeType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        protoInput = mCollectionSchemeList->getData();
        logStr = "The CollectionSchemeList";
        break;
    case DataType::DECODER_MANIFEST:
        protoInput = mDecoderManifest->getData();
        logStr = "The DecoderManifest";
        break;
    default:
        mLogger.error( "CollectionSchemeManager::store",
                       "cannot store unsupported type of " + std::to_string( toUType( storeType ) ) );
        return;
    }

    if ( protoInput.empty() )
    {
        mLogger.error( "CollectionSchemeManager::store", logStr + " data size is zero." );
        return;
    }
    ret = mSchemaPersistency->write( protoInput.data(), protoInput.size(), storeType );
    if ( ret != ErrorCode::SUCCESS )
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
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
