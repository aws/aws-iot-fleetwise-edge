// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeIngestionList.h"
#include "CollectionSchemeManager.h" // IWYU pragma: associated
#include "DecoderManifestIngestion.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include <cstddef>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

void
CollectionSchemeManager::prepareCheckinTimer()
{
    auto currTime = mClock->timeSinceEpoch();
    TimeData checkinData = TimeData{ currTime, CHECKIN };
    mTimeLine.push( checkinData );
}

bool
CollectionSchemeManager::sendCheckin()
{
    // Create a list of active collectionSchemes and the current decoder manifest and send it to cloud
    std::vector<std::string> checkinMsg;
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); it++ )
    {
        checkinMsg.emplace_back( it->first );
    }
    for ( auto it = mIdleCollectionSchemeMap.begin(); it != mIdleCollectionSchemeMap.end(); it++ )
    {
        checkinMsg.emplace_back( it->first );
    }
    if ( !mCurrentDecoderManifestID.empty() )
    {
        checkinMsg.emplace_back( mCurrentDecoderManifestID );
    }
    std::string checkinLogStr;
    for ( size_t i = 0; i < checkinMsg.size(); i++ )
    {
        if ( i > 0 )
        {
            checkinLogStr += ", ";
        }
        checkinLogStr += checkinMsg[i];
    }
    FWE_LOG_TRACE( "CHECKIN: " + checkinLogStr );

    if ( mSchemaListenerPtr == nullptr )
    {
        FWE_LOG_ERROR( "Cannot set the checkin message" );
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
        FWE_LOG_ERROR( "Failed to acquire a valid handle on the scheme local persistency module" );
        return false;
    }
    switch ( retrieveType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        infoStr = "Retrieved a CollectionSchemeList of size ";
        errStr = "Failed to retrieve the CollectionSchemeList from the persistency module due to an error: ";
        break;
    case DataType::DECODER_MANIFEST:
        infoStr = "Retrieved a DecoderManifest of size ";
        errStr = "Failed to retrieve the DecoderManifest from the persistency module due to an error: ";
        break;
    default:
        FWE_LOG_ERROR( "Unknown error: " + std::to_string( toUType( retrieveType ) ) );
        return false;
    }

    protoSize = mSchemaPersistency->getSize( retrieveType );
    if ( protoSize <= 0 )
    {
        FWE_LOG_INFO( infoStr + "zero" );
        return false;
    }
    protoOutput.resize( protoSize );
    ret = mSchemaPersistency->read( protoOutput.data(), protoSize, retrieveType );
    if ( ret != ErrorCode::SUCCESS )
    {
        auto error = mSchemaPersistency->getErrorString( ret );
        errStr += error != nullptr ? error : "Unknown error";
        FWE_LOG_ERROR( errStr );
        return false;
    }
    FWE_LOG_INFO( infoStr + std::to_string( protoSize ) + " successfully" );
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
    // currently this if will be always true as it can be only DECODER_MANIFEST or COLLECTION_SCHEME_LIST but for
    // readability leave it as else if instead of else
    // coverity[autosar_cpp14_m0_1_2_violation]
    // coverity[autosar_cpp14_m0_1_9_violation]
    // coverity[misra_cpp_2008_rule_0_1_9_violation]
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
        FWE_LOG_ERROR( "Failed to acquire a valid handle on the scheme local persistency module" );
        return;
    }
    if ( ( storeType == DataType::COLLECTION_SCHEME_LIST ) && ( mCollectionSchemeList == nullptr ) )
    {
        FWE_LOG_ERROR( "Invalid CollectionSchemeList" );
        return;
    }
    if ( ( storeType == DataType::DECODER_MANIFEST ) && ( mDecoderManifest == nullptr ) )
    {
        FWE_LOG_ERROR( "Invalid DecoderManifest" );
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
        FWE_LOG_ERROR( "cannot store unsupported type of " + std::to_string( toUType( storeType ) ) );
        return;
    }

    if ( protoInput.empty() )
    {
        FWE_LOG_ERROR( logStr + " data size is zero" );
        return;
    }
    ret = mSchemaPersistency->write( protoInput.data(), protoInput.size(), storeType );
    if ( ret != ErrorCode::SUCCESS )
    {
        logStr += " because of this error: ";
        auto error = mSchemaPersistency->getErrorString( ret );
        logStr += error != nullptr ? error : "Unknown error";
        FWE_LOG_ERROR( "failed to persist " + logStr );
    }
    else
    {
        FWE_LOG_TRACE( logStr + " persisted successfully" );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
